#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/LightShadowDepthPass.h"
#include "VulkanRenderEngine/General/RenderHelp.h"

constexpr uint32_t batch_max = 16;

constexpr uint32_t maxUpdateDeltaMs = 25;
constexpr float maxUpdateDeltaTime = maxUpdateDeltaMs * 1000.f;

struct alignas(16) DirLightMetaInfo {
	alignas(16) glm::vec3 direction;
	alignas(16) glm::vec3 color;

	float luxIntensity;

	uint32_t castShadow;
	int cascadeFirst;
	int cascadeCount;
};

struct alignas(16) DirLightCascadeInfo {
	alignas(16) glm::mat4 lightSpaceMatrix;
	int atlasX;
	int atlasY;
	int atlasWidth;
	int atlasHeight;
	float cascadePlaneDistance;
};

struct SpotLightMetaInfo {
	alignas(16) glm::vec3 position;
	alignas(16) glm::vec3 color;
	alignas(16) glm::vec3 direction;

	float cdIntensity;

	uint32_t castShadow;
	float cutOff;
	float outerCutOff;

	float radius;

	int atlasX;
	int atlasY;
	int atlasWidth;
	int atlasHeight;

	alignas(16) glm::mat4 lightSpaceMatrix;
};

struct alignas(16) PointLightMetaInfo {
	alignas(16) glm::vec3 position;
	alignas(16) glm::vec3 color;
	alignas(16) glm::vec2 atlasStart[6];
	alignas(16) glm::vec2 atlasSize[6];
	uint32_t castShadow;
	float cdIntensity;
	float radius;
};

static bool GetCubeViewPorts(std::array<DynamicViewport, 6>& viewports, const std::shared_ptr<PointLightInfo>& info, const AtlasMap& atlasShadowMap)
{
	if (!info || !info->atlas)
		return false;

	for (int i = 0; i < 6; i++)
	{
		AtlasMap::AtlasRect rect;
		if (!atlasShadowMap.GetSpace(info->atlas->ids[i], rect))
			return false;

		viewports[i].x = rect.x;
		viewports[i].y = rect.y;
		viewports[i].width = rect.width;
		viewports[i].height = rect.height;
	}
	return true;
}

static inline void writePaddingCount(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, uint32_t count, std::shared_ptr<StorageBlock>& ssbo) {
	glm::ivec4 padding = glm::ivec4(count, 0.f, 0.f, 0.f);
	ssbo->WriteDataAsync(cmd, &padding, sizeof(padding), 0);
};

static void SetupDirLightData(
	std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
	std::shared_ptr<StorageBlock>& meta_ssbo,
	std::shared_ptr<StorageBlock>& cascade_ssbo,
	const std::vector<std::shared_ptr<DirLightInfo>>& dirLights,
	const std::shared_ptr<AtlasMap>& atlasShadowMap
)
{

	uint32_t cascadeOffset = 0;
	std::vector<DirLightMetaInfo> dirLightMetaInfos;
	std::vector<DirLightCascadeInfo> dirLightCascadeInfos;
	for (auto& info : dirLights)
	{
		if (!info || !info->light)
			continue;

		auto& light = info->light;

		DirLightMetaInfo metainfo;
		metainfo.direction = light->getDirection();
		metainfo.color = light->getColor();

		metainfo.luxIntensity = light->getIntensity();

		metainfo.castShadow = info->light->getCastShadow() || !info->cascades.empty();
		if (metainfo.castShadow)
		{
			std::vector<DirLightCascadeInfo> cascadeInfos;
			cascadeInfos.reserve(info->cascades.size());
			for (auto& cascade : info->cascades)
			{

				AtlasMap::AtlasRect rect;
				if (!atlasShadowMap->GetSpace(cascade.id, rect))
					continue;

				DirLightCascadeInfo cascadeinfo;
				cascadeinfo.atlasX = rect.x;
				cascadeinfo.atlasY = rect.y;
				cascadeinfo.atlasWidth = rect.width;
				cascadeinfo.atlasHeight = rect.height;
				cascadeinfo.cascadePlaneDistance = cascade.cascadePlaneDistance;
				cascadeinfo.lightSpaceMatrix = cascade.lightSpaceMatrix;

				cascadeInfos.push_back(cascadeinfo);
			}

			metainfo.cascadeFirst = cascadeOffset;
			metainfo.cascadeCount = cascadeInfos.size();

			if (!cascadeInfos.empty())
				dirLightCascadeInfos.append_range(cascadeInfos);

			cascadeOffset += metainfo.cascadeCount;
		}
		else
		{
			metainfo.cascadeFirst = 0;
			metainfo.cascadeCount = 0;
		}

		dirLightMetaInfos.push_back(metainfo);
	}

	meta_ssbo->SetSizeAsync(cmd, 16 + dirLightMetaInfos.size() * sizeof(DirLightMetaInfo));
	writePaddingCount(cmd, dirLightMetaInfos.size(), meta_ssbo);
	meta_ssbo->Barrier(cmd, BufferUsage::TransferWrite);
	meta_ssbo->WriteDataAsync(cmd, dirLightMetaInfos.data(), dirLightMetaInfos.size() * sizeof(DirLightMetaInfo), 16);

	cascade_ssbo->SetSizeAsync(cmd, 16 + dirLightCascadeInfos.size() * sizeof(DirLightCascadeInfo));
	writePaddingCount(cmd, dirLightCascadeInfos.size(), cascade_ssbo);
	cascade_ssbo->Barrier(cmd, BufferUsage::TransferWrite);
	cascade_ssbo->WriteDataAsync(cmd, dirLightCascadeInfos.data(), dirLightCascadeInfos.size() * sizeof(DirLightCascadeInfo), 16);
}

static void SetupPointLightData(
	std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
	std::shared_ptr<StorageBlock>& meta_ssbo,
	const std::vector<std::shared_ptr<PointLightInfo>>& pointLights,
	const std::shared_ptr<AtlasMap>& atlasShadowMap)
{

	std::vector<PointLightMetaInfo> pointLightMetaInfos;
	for (auto& info : pointLights)
	{
		auto light = info->light;
		bool castShadow = light->getCastShadow();

		std::array<DynamicViewport, 6> viewports;
		if (castShadow && !GetCubeViewPorts(viewports, info, *atlasShadowMap))
			continue;

		PointLightMetaInfo metainfo;
		metainfo.position = light->getPosition();
		metainfo.color = light->getColor();
		metainfo.castShadow = castShadow;
		metainfo.cdIntensity = light->getIntensity();
		metainfo.radius = light->getRadius();
		for (int i = 0; i < 6; i++)
		{
			auto& viewport = viewports[i];
			metainfo.atlasStart[i] = glm::vec2(viewport.x, viewport.y);
			metainfo.atlasSize[i] = glm::vec2(viewport.width, viewport.height);
		}
		pointLightMetaInfos.push_back(metainfo);
	}

	meta_ssbo->SetSizeAsync(cmd, 16 + pointLightMetaInfos.size() * sizeof(PointLightMetaInfo));
	writePaddingCount(cmd, pointLightMetaInfos.size(), meta_ssbo);
	meta_ssbo->Barrier(cmd, BufferUsage::TransferWrite);
	meta_ssbo->WriteDataAsync(cmd, pointLightMetaInfos.data(), pointLightMetaInfos.size() * sizeof(PointLightMetaInfo), 16);
}

static void SetupSpotLightData(
	std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
	std::shared_ptr<StorageBlock>& meta_ssbo,
	const std::vector<std::shared_ptr<SpotLightInfo>>& spotLights,
	const std::shared_ptr<AtlasMap>& atlasShadowMap)
{

	std::vector<SpotLightMetaInfo> spotLightMetaInfos;
	for (auto& info : spotLights)
	{
		if (!info || !info->light)
			continue;

		auto light = info->light;
		bool castShadow = light->getCastShadow();

		AtlasMap::AtlasRect rect;
		if (castShadow && (!info->atlas || !atlasShadowMap->GetSpace(info->atlas->id, rect)))
			continue;

		SpotLightMetaInfo metainfo;
		metainfo.position = light->getPosition();
		metainfo.color = light->getColor();
		metainfo.cdIntensity = light->getIntensity();
		metainfo.castShadow = castShadow;
		metainfo.direction = light->getDirection();
		metainfo.cutOff = glm::cos(glm::radians(light->getCutOffAngle()));
		metainfo.outerCutOff = glm::cos(glm::radians(light->getOuterCutOffAngle()));
		metainfo.lightSpaceMatrix = light->getLightSpaceMatrix();
		metainfo.radius = light->getRadius();
		metainfo.atlasX = rect.x;
		metainfo.atlasY = rect.y;
		metainfo.atlasWidth = rect.width;
		metainfo.atlasHeight = rect.height;

		spotLightMetaInfos.push_back(std::move(metainfo));
	}

	meta_ssbo->SetSizeAsync(cmd, 16 + spotLightMetaInfos.size() * sizeof(SpotLightMetaInfo));
	writePaddingCount(cmd, spotLightMetaInfos.size(), meta_ssbo);
	meta_ssbo->Barrier(cmd, BufferUsage::TransferWrite);
	meta_ssbo->WriteDataAsync(cmd, spotLightMetaInfos.data(), spotLightMetaInfos.size() * sizeof(SpotLightMetaInfo), 16);
}

struct CascadeSplit
{
	float nearPlane;
	float farPlane;
	glm::mat4 projection;
};

static std::vector<CascadeSplit> CalculateCascadeSplit(
	float fov,
	float aspect,
	float nearPlane,
	float farPlane,
	int cascadeCount,
	float lambda = 0.75f)  // lambda控制对数/均匀混合，0.5常用
{
	if (cascadeCount <= 0)
		return {};

	std::vector<CascadeSplit> splits;
	splits.reserve(cascadeCount);

	for (int i = 0; i < cascadeCount; ++i) {
		float t0 = static_cast<float>(i) / cascadeCount;
		float t1 = static_cast<float>(i + 1) / cascadeCount;

		float logN = nearPlane * std::pow(farPlane / nearPlane, t0);
		float logF = nearPlane * std::pow(farPlane / nearPlane, t1);

		float uniN = nearPlane + (farPlane - nearPlane) * t0;
		float uniF = nearPlane + (farPlane - nearPlane) * t1;

		// 混合
		float n = lambda * logN + (1.0f - lambda) * uniN;
		float f = lambda * logF + (1.0f - lambda) * uniF;

		glm::mat4 proj = vkPerspective(glm::radians(fov), aspect, n, f);
		splits.push_back({ n, f,proj });
	}

	return splits;
}

struct alignas(16) LightProp
{
	glm::vec3 lightPos;
	float farPlane;
};

LightShadowDepthPass::LightShadowDepthPass(
	const std::string& dirLightShadowStaticMeshVertexShaderPath,
	const std::string& dirLightShadowSkinnedMeshVertexShaderPath,
	const std::string& dirLightShadowFragmentShaderPath,
	const std::string& pointLightShadowStaticMeshVertexShaderPath,
	const std::string& pointLightShadowSkinnedMeshVertexShaderPath,
	const std::string& pointLightShadowFragmentShaderPath
)
{

	{
		_dirLightShadowDepthStaticMeshShader = std::make_shared<GraphicsPipeline>();

		GraphicsPipelineConfig config;
		config.vertexPath = dirLightShadowStaticMeshVertexShaderPath;
		config.fragmentPath = dirLightShadowFragmentShaderPath;

		config.AddVertexInputAttributeDescription(Vertex::GetVertexInputAttributeDescription());
		config.AddVertexInputBindingDescription(Vertex::GetVertexInputBindingDescription());

		config.SetDepthAttachmentFormat(vk::Format::eD32Sfloat);
		config.SetStencilAttachmentFormat(vk::Format::eUndefined);

		config
			.AddBindlessMaterialTextureBinding()
			.AddStorageBuffer(4)
			.AddStorageBuffer(5);

		config.AddDynamicState(vk::DynamicState::eCullMode);

		if (config.Validate())
			_dirLightShadowDepthStaticMeshShader->Create(config);
	}

	{
		_dirLightShadowDepthSkinnedShader = std::make_shared<GraphicsPipeline>();

		GraphicsPipelineConfig config;
		config.vertexPath = dirLightShadowSkinnedMeshVertexShaderPath;
		config.fragmentPath = dirLightShadowFragmentShaderPath;

		config.AddVertexInputAttributeDescription(Vertex::GetVertexInputAttributeDescription());
		config.AddVertexInputBindingDescription(Vertex::GetVertexInputBindingDescription());

		config.SetDepthAttachmentFormat(vk::Format::eD32Sfloat);
		config.SetStencilAttachmentFormat(vk::Format::eUndefined);

		config
			.AddBindlessMaterialTextureBinding()
			.AddAnimationDataBinding()
			.AddStorageBuffer(4)
			.AddStorageBuffer(5);

		config.AddDynamicState(vk::DynamicState::eCullMode);

		if (config.Validate())
			_dirLightShadowDepthSkinnedShader->Create(config);
	}

	{
		_pointLightShadowDepthStaticMeshShader = std::make_shared<GraphicsPipeline>();

		GraphicsPipelineConfig config;
		config.vertexPath = pointLightShadowStaticMeshVertexShaderPath;
		config.fragmentPath = pointLightShadowFragmentShaderPath;

		config.AddVertexInputAttributeDescription(Vertex::GetVertexInputAttributeDescription());
		config.AddVertexInputBindingDescription(Vertex::GetVertexInputBindingDescription());

		config.SetDepthAttachmentFormat(vk::Format::eD32Sfloat);
		config.SetStencilAttachmentFormat(vk::Format::eUndefined);

		config
			.AddBindlessMaterialTextureBinding()
			.AddStorageBuffer(4)
			.AddStorageBuffer(5)
			.AddStorageBuffer(6);

		config.AddDynamicState(vk::DynamicState::eCullMode);

		if (config.Validate())
			_pointLightShadowDepthStaticMeshShader->Create(config);
	}

	{
		_pointLightShadowDepthSkinnedShader = std::make_shared<GraphicsPipeline>();

		GraphicsPipelineConfig config;
		config.vertexPath = pointLightShadowSkinnedMeshVertexShaderPath;
		config.fragmentPath = pointLightShadowFragmentShaderPath;

		config.AddVertexInputAttributeDescription(Vertex::GetVertexInputAttributeDescription());
		config.AddVertexInputBindingDescription(Vertex::GetVertexInputBindingDescription());

		config.SetDepthAttachmentFormat(vk::Format::eD32Sfloat);
		config.SetStencilAttachmentFormat(vk::Format::eUndefined);

		config
			.AddBindlessMaterialTextureBinding()
			.AddAnimationDataBinding()
			.AddStorageBuffer(4)
			.AddStorageBuffer(5)
			.AddStorageBuffer(6);

		config.AddDynamicState(vk::DynamicState::eCullMode);

		if (config.Validate())
			_pointLightShadowDepthSkinnedShader->Create(config);
	}

}

LightShadowDepthPass::~LightShadowDepthPass()
{}

bool LightShadowDepthPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return true;
}

void LightShadowDepthPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	auto selfctx = registry.Get<SelfContext>("context");
	state.lights.shadowAtlas = selfctx->atlas;

	CalculateShadowAtlas(state, *selfctx->atlas);
	auto cmd = VKCONTEXT->GetCommandBuffer();
	SetupLightingData(*selfctx, cmd, state);
	if (cmd->IsRecording())
		cmd->SubmitNowAndWait();

	state.lights.ssbo_dirLightMeta = selfctx->ssbo_dirLightMeta;
	state.lights.ssbo_dirLightCascade = selfctx->ssbo_dirLightCascade;
	state.lights.ssbo_pointLightMeta = selfctx->ssbo_pointLightMeta;
	state.lights.ssbo_spotLightMeta = selfctx->ssbo_spotLightMeta;
}

void LightShadowDepthPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	auto shadowAtlas = ctx.GetOutput(0);
	if (!shadowAtlas)
		return;

	if (state.lights.dirLightInfos.empty() && state.lights.spotLightInfos.empty() && state.lights.pointLightInfos.empty())
		return;

	auto selfctx = registry.Get<SelfContext>("context");

	selfctx->staticMesh_OneSideCommands = state.indirectCommands.staticMesh_OneSideCommand;
	selfctx->staticMesh_TwoSideCommands = state.indirectCommands.staticMesh_TwoSideCommand;

	glm::u32vec2 size = glm::max(selfctx->atlas->GetSize(), glm::u32vec2(16, 16));
	shadowAtlas->Resize(size.x, size.y);


	auto cmd = cmdCtx.GetCmd();
	shadowAtlas->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	if (cmd->IsRecording())
		cmd->SubmitToQueue();

	DynamicRenderInfo renderInfo;
	renderInfo
		.SetRenderArea(shadowAtlas->GetWidth(), shadowAtlas->GetHeight())
		.AddDepthAttachment(shadowAtlas->GetImageView(vk::ImageAspectFlagBits::eDepth));

	processDirAndSpotLight(*selfctx, state, cmd, renderInfo);
	processPointLight(*selfctx, state, cmd, renderInfo);
}

void LightShadowDepthPass::FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{}

void LightShadowDepthPass::CalculateShadowAtlas(RenderState& state, AtlasMap& atlas)
{
	auto& dirLightInfos = state.lights.dirLightInfos;
	auto& spotLightInfos = state.lights.spotLightInfos;
	auto& pointLightInfos = state.lights.pointLightInfos;

	for (auto& info : dirLightInfos)
	{
		if (!info || !info->light || !info->light->getCastShadow())continue;
		auto& light = info->light;
		for (int i = 0; i < light->getCascadeLevel(); i++)
		{
			uint32_t id;
			if (atlas.AllocateSpace(light->getShadowMapWidth(), light->getShadowMapHeight(), id))
				info->cascades.push_back({ id ,0 });
			else
				break;
		}

		float aspect = fabs(state.camera.projection[1][1] / state.camera.projection[0][0]);
		auto cascadeSplit = CalculateCascadeSplit(state.camera.fov, aspect, state.camera.nearPlane, state.camera.farPlane, info->cascades.size());

		for (int i = 0; i < cascadeSplit.size(); i++)
		{
			info->cascades[i].lightSpaceMatrix = info->light->getLightSpaceMatrixWithFrustumCorners(cascadeSplit[i].projection, state.camera.view, &info->cascades[i].center, &info->cascades[i].radius);
			info->cascades[i].cascadePlaneDistance = cascadeSplit[i].farPlane;
		}
	}

	for (auto& info : spotLightInfos)
	{
		if (!info || !info->light || !info->light->getCastShadow())continue;
		auto& light = info->light;
		uint32_t id;
		if (atlas.AllocateSpace(light->getShadowMapWidth(), light->getShadowMapHeight(), id))
		{
			info->atlas = std::make_shared<SpotLightInfo::Atlas>();
			info->atlas->id = id;
		}
	}

	for (auto& info : pointLightInfos)
	{
		if (!info || !info->light || !info->light->getCastShadow())continue;
		auto& light = info->light;
		uint32_t id;

		info->atlas = std::make_shared<PointLightInfo::Atlas>();
		for (int i = 0; i < 6; i++)
		{
			if (atlas.AllocateSpace(light->getShadowMapWidth(), light->getShadowMapHeight(), info->atlas->ids[i]))
				info->atlas->enable[i] = true;
		}
	}

}

void LightShadowDepthPass::processDirAndSpotLight(SelfContext& ctx, RenderState& state, const std::shared_ptr<RenderGraph::PassFrameCmd>& cmd, DynamicRenderInfo& renderInfo)
{
	auto& dirLightsInfo = state.lights.dirLightInfos;
	auto& spotLightsInfo = state.lights.spotLightInfos;

	if (dirLightsInfo.empty() && spotLightsInfo.empty())
		return;

	GraphicsBindingRecord dirLightShadowDepthStaticMeshBinding;
	GraphicsBindingRecord dirLightShadowDepthSkinnedBinding;

	dirLightShadowDepthStaticMeshBinding.SetStorageBlock(ctx.ssbo_ShadowMatrices, 4);
	dirLightShadowDepthStaticMeshBinding.SetStorageBlock(state.indirectCommands.ssbo_StaticMesh_TransformAndMaterialIndices, 5);
	dirLightShadowDepthStaticMeshBinding.SetBindlessMaterialTexture(IndirectDrawManager::Instance()->GetMaterialSSBO(), BindlessTextureManager::Instance());

	dirLightShadowDepthSkinnedBinding.SetStorageBlock(ctx.ssbo_ShadowMatrices, 4);

	std::vector<glm::mat4> shadowMatrices;
	shadowMatrices.reserve(batch_max);
	std::vector<DynamicViewport> viewPorts;
	viewPorts.reserve(batch_max);

	uint32_t count = 0;

	auto render = [&]()->void {
		ctx.ssbo_ShadowMatrices->WriteDataAsync(cmd, shadowMatrices.data(), shadowMatrices.size() * sizeof(glm::mat4));
		ctx.ssbo_ShadowMatrices->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageRead);
		RenderSceneLightShadowPassSceneInstance(
			ctx,
			cmd,
			state,
			_dirLightShadowDepthStaticMeshShader,
			_dirLightShadowDepthSkinnedShader,
			dirLightShadowDepthStaticMeshBinding,
			dirLightShadowDepthSkinnedBinding,
			count,
			state.objects.sceneRenderData.opaqueMesh,
			state.objects.sceneRenderData.opaqueSkinnedModel,
			renderInfo,
			viewPorts
		);
		ctx.ssbo_ShadowMatrices->Barrier(cmd, BufferUsage::StorageRead, BufferUsage::TransferWrite);
		if (cmd->IsRecording())
			cmd->SubmitNow();
		count = 0;
		shadowMatrices.clear();
		viewPorts.clear();
		};

	for (auto& info : dirLightsInfo)
	{
		if (!info || !info->light || info->cascades.empty() || !info->light->getCastShadow())
			continue;

		for (auto& cascade : info->cascades)
		{
			AtlasMap::AtlasRect rect;
			if (!ctx.atlas->GetSpace(cascade.id, rect))
				continue;

			int level = info->light->getCascadeLevel();

			shadowMatrices.push_back(cascade.lightSpaceMatrix);
			viewPorts.push_back({ rect.width, rect.height, rect.x ,rect.y });

			count++;
			if (count >= batch_max)
				render();
		}
	}

	for (auto& info : spotLightsInfo)
	{
		if (!info || !info->light || !info->atlas || !info->light->getCastShadow())
			continue;

		auto& light = info->light;

		if (!state.camera.frustum.IsSphereOnFrustum(light->getPosition(), light->getRadius()))
			continue;

		auto mat = light->getLightSpaceMatrix();
		Frustum lightFrustm(mat);
		if (!state.camera.frustum.IsIntersectsFrustum(lightFrustm))
			continue;

		AtlasMap::AtlasRect rect;
		if (!ctx.atlas->GetSpace(info->atlas->id, rect))
			continue;

		shadowMatrices.push_back(mat);
		viewPorts.push_back({ rect.width, rect.height, rect.x ,rect.y });

		count++;
		if (count >= batch_max)
			render();
	}

	if (count > 0)
		render();
}

void LightShadowDepthPass::processPointLight(SelfContext& ctx, RenderState& state, const std::shared_ptr<RenderGraph::PassFrameCmd>& cmd, DynamicRenderInfo& renderInfo)
{
	auto& pointLightsInfo = state.lights.pointLightInfos;

	if (pointLightsInfo.empty())
		return;

	GraphicsBindingRecord pointLightShadowDepthStaticMeshBinding;
	GraphicsBindingRecord pointLightShadowDepthSkinnedBinding;

	pointLightShadowDepthStaticMeshBinding.SetStorageBlock(ctx.ssbo_ShadowMatrices, 4);
	pointLightShadowDepthStaticMeshBinding.SetStorageBlock(state.indirectCommands.ssbo_StaticMesh_TransformAndMaterialIndices, 5);
	pointLightShadowDepthStaticMeshBinding.SetStorageBlock(ctx.ssbo_LightProps, 6);
	pointLightShadowDepthStaticMeshBinding.SetBindlessMaterialTexture(IndirectDrawManager::Instance()->GetMaterialSSBO(), BindlessTextureManager::Instance());

	pointLightShadowDepthSkinnedBinding.SetStorageBlock(ctx.ssbo_ShadowMatrices, 4);
	pointLightShadowDepthSkinnedBinding.SetStorageBlock(ctx.ssbo_LightProps, 6);

	std::vector<glm::mat4> shadowMatrices;
	shadowMatrices.reserve(batch_max);
	std::vector<DynamicViewport> viewPorts;
	viewPorts.reserve(batch_max);
	std::vector<LightProp> lightProps;
	lightProps.reserve(batch_max);

	uint32_t count = 0;

	auto render = [&]()->void {
		ctx.ssbo_ShadowMatrices->WriteDataAsync(cmd, shadowMatrices.data(), shadowMatrices.size() * sizeof(glm::mat4));
		ctx.ssbo_LightProps->WriteDataAsync(cmd, lightProps.data(), lightProps.size() * sizeof(LightProp));
		ctx.ssbo_ShadowMatrices->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageRead);
		ctx.ssbo_LightProps->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageRead);
		RenderSceneLightShadowPassSceneInstance(
			ctx,
			cmd,
			state,
			_pointLightShadowDepthStaticMeshShader,
			_pointLightShadowDepthSkinnedShader,
			pointLightShadowDepthStaticMeshBinding,
			pointLightShadowDepthSkinnedBinding,
			count,
			state.objects.sceneRenderData.opaqueMesh,
			state.objects.sceneRenderData.opaqueSkinnedModel,
			renderInfo,
			viewPorts
		);
		ctx.ssbo_ShadowMatrices->Barrier(cmd, BufferUsage::StorageRead, BufferUsage::TransferWrite);
		ctx.ssbo_LightProps->Barrier(cmd, BufferUsage::StorageRead, BufferUsage::TransferWrite);
		if (cmd->IsRecording())
			cmd->SubmitNow();
		count = 0;
		shadowMatrices.clear();
		viewPorts.clear();
		lightProps.clear();
		};

	for (auto& info : pointLightsInfo)
	{
		if (!info || !info->light || !info->light->getCastShadow())
			continue;

		std::array<DynamicViewport, 6> viewports;
		if (!GetCubeViewPorts(viewports, info, *ctx.atlas))
			continue;

		auto light = info->light;

		if (!state.camera.frustum.IsSphereOnFrustum(light->getPosition(), light->getRadius()))
			continue;

		float shadowMapWidth = light->getShadowMapWidth(),
			shadowMapHeight = light->getShadowMapHeight();

		float aspect = (float)shadowMapWidth / (float)shadowMapHeight;
		float near_plane = 0.1f;
		float far_plane = light->getRadius();
		glm::mat4 shadowProj = vkPerspective(glm::radians(90.0f), aspect, near_plane, far_plane);
		glm::vec3 lightPos = light->getPosition();

		std::vector<glm::mat4> shadowTransforms;
		shadowTransforms.push_back(shadowProj *
			glm::lookAt(lightPos, lightPos + glm::vec3(1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0)));
		shadowTransforms.push_back(shadowProj *
			glm::lookAt(lightPos, lightPos + glm::vec3(-1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0)));
		shadowTransforms.push_back(shadowProj *
			glm::lookAt(lightPos, lightPos + glm::vec3(0.0, 1.0, 0.0), glm::vec3(0.0, 0.0, 1.0)));
		shadowTransforms.push_back(shadowProj *
			glm::lookAt(lightPos, lightPos + glm::vec3(0.0, -1.0, 0.0), glm::vec3(0.0, 0.0, -1.0)));
		shadowTransforms.push_back(shadowProj *
			glm::lookAt(lightPos, lightPos + glm::vec3(0.0, 0.0, 1.0), glm::vec3(0.0, -1.0, 0.0)));
		shadowTransforms.push_back(shadowProj *
			glm::lookAt(lightPos, lightPos + glm::vec3(0.0, 0.0, -1.0), glm::vec3(0.0, -1.0, 0.0)));

		for (int face = 0; face < 6; face++)
		{
			Frustum faceFrustm(shadowTransforms[face]);
			if (!state.camera.frustum.IsIntersectsFrustum(faceFrustm))
				continue;

			shadowMatrices.push_back(shadowTransforms[face]);
			lightProps.push_back({ lightPos ,far_plane });
			viewPorts.push_back(viewports[face]);

			count++;
			if (count >= batch_max)
				render();
		}
	}

	if (count > 0)
		render();
}

void LightShadowDepthPass::RenderSceneLightShadowPassSceneInstance(
	SelfContext& ctx,
	const std::shared_ptr<RenderGraph::PassFrameCmd>& cmd,
	RenderState& state,
	std::shared_ptr<GraphicsPipeline>& shader_StaticMesh,
	std::shared_ptr<GraphicsPipeline>& shader_Skinned,
	GraphicsBindingRecord& shader_StaticMesh_Binding,
	GraphicsBindingRecord& shader_Skinned_Binding,
	uint32_t count,
	std::vector<VKRenderObjectData::SceneRenderData::OpaqueMeshItem>& opaqueMeshes,
	std::vector<VKRenderObjectData::SceneRenderData::OpaqueSkinnedModelItem>& opaqueSinnedModels,
	DynamicRenderInfo& renderInfo,
	std::vector<DynamicViewport>& viewPorts
)
{
	auto manager = IndirectDrawManager::Instance();

	if (!opaqueMeshes.empty())
	{
		auto& oneSideCommands = ctx.staticMesh_OneSideCommands;
		auto& twoSideCommands = ctx.staticMesh_TwoSideCommands;

		if (!oneSideCommands.empty() || !twoSideCommands.empty())
		{
			auto& shader = shader_StaticMesh;

			shader->Bind(cmd, shader_StaticMesh_Binding);

			if (!oneSideCommands.empty())
			{
				std::for_each(std::execution::par_unseq, oneSideCommands.begin(), oneSideCommands.end(),
					[&](IndirectDrawCommand& command)->void {
						if (command.instanceCount > 0)
							command.instanceCount = count;
					}
				);
				ctx.oneSideCommandBuffer.WriteDataAsync(cmd, oneSideCommands.data(), oneSideCommands.size() * sizeof(IndirectDrawCommand));
				ctx.oneSideCommandBuffer.Barrier(cmd, BufferUsage::TransferWrite);
			}

			if (!twoSideCommands.empty())
			{
				std::for_each(std::execution::par_unseq, twoSideCommands.begin(), twoSideCommands.end(),
					[&](IndirectDrawCommand& command)->void {
						if (command.instanceCount > 0)
							command.instanceCount = count;
					}
				);
				ctx.twoSideCommandBuffer.WriteDataAsync(cmd, twoSideCommands.data(), twoSideCommands.size() * sizeof(IndirectDrawCommand));
				ctx.twoSideCommandBuffer.Barrier(cmd, BufferUsage::TransferWrite);
			}

			cmd->setDynamicViewports(viewPorts);
			cmd->beginRendering(renderInfo);
			if (renderInfo.depthAttachment->loadOp == vk::AttachmentLoadOp::eClear)
				renderInfo.depthAttachment->loadOp = vk::AttachmentLoadOp::eLoad;

			cmd->bindVertexBuffers(manager->GetVertexBlock());
			cmd->bindIndexBuffer(manager->GetIndexBlock());
			cmd->setCullMode(vk::CullModeFlagBits::eNone);


			if (!oneSideCommands.empty())
			{
				cmd->drawIndexedIndirect(ctx.oneSideCommandBuffer, oneSideCommands.size());
				ctx.oneSideCommandBuffer.Barrier(cmd, BufferUsage::IndirectRead, BufferUsage::TransferWrite);
			}

			if (!twoSideCommands.empty())
			{
				cmd->drawIndexedIndirect(ctx.twoSideCommandBuffer, twoSideCommands.size());
				ctx.twoSideCommandBuffer.Barrier(cmd, BufferUsage::IndirectRead, BufferUsage::TransferWrite);
			}

			cmd->endRendering();
		}
	}

	//if (!opaqueSinnedModels.empty())
	//{
	//  cmd2 = VKCONTEXT->GetCommandBuffer();
	// 	fence2 = std::make_shared<VKWrapper::VKFence>(VKCONTEXT->GetDevice().get());
	//	auto& shader = shader_Skinned;

	//	shader.Use();
	//	RenderHelp::SetupAnimatorGroupData(shader, {});

	//	glm::mat4 cur_Model = glm::mat4(1.0f);
	//	shader.setMat4("model", cur_Model);

	//	for (size_t i = 0; i < opaqueSinnedModels.size(); i++)
	//	{
	//		auto& model = opaqueSinnedModels[i];

	//		if (cur_Model != model.transform)
	//		{
	//			shader.setMat4("model", model.transform);
	//			cur_Model = model.transform;
	//		}
	//		RenderHelp::SetupAnimatorGroupData(shader, *model.animators);

	//		for (auto& meshinfo : model.models)
	//		{
	//			meshinfo.DrawGeometryInstanced(shader, count);
	//		}
	//	}
	//}
}

void LightShadowDepthPass::SetupLightingData(SelfContext& ctx, std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state)
{
	SetupDirLightData(cmd, ctx.ssbo_dirLightMeta, ctx.ssbo_dirLightCascade, state.lights.dirLightInfos, ctx.atlas);
	SetupPointLightData(cmd, ctx.ssbo_pointLightMeta, state.lights.pointLightInfos, ctx.atlas);
	SetupSpotLightData(cmd, ctx.ssbo_spotLightMeta, state.lights.spotLightInfos, ctx.atlas);
}
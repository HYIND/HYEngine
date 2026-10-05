#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/LightShadowDepthPass.h"
#include "VulkanRenderEngine/General/RenderHelp.h"

constexpr uint32_t batch_max = 16;

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
	std::shared_ptr<StorageBlock>& cascadeDstances_ssbo,
	const std::vector<std::shared_ptr<DirLightInfo>>& dirLights,
	const std::shared_ptr<AtlasMap>& atlasShadowMap
)
{

	uint32_t cascadeOffset = 0;
	std::vector<DirLightMetaInfo> dirLightMetaInfos;
	std::vector<DirLightCascadeInfo> dirLightCascadeInfos;
	std::vector<float> dirLightCascadeDistances;
	for (auto& info : dirLights)
	{
		if (!info || !info->light)
			continue;

		auto& light = info->light;

		DirLightMetaInfo metainfo;
		metainfo.direction = light->GetDirection();
		metainfo.color = light->GetColor();

		metainfo.luxIntensity = light->GetIntensity();

		metainfo.castShadow = info->light->GetCastShadow() || !info->cascades.empty();
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
				cascadeinfo.lightSpaceMatrix = cascade.lightSpaceMatrix;

				cascadeInfos.push_back(cascadeinfo);
				dirLightCascadeDistances.push_back(cascade.cascadePlaneDistance);
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

	cascadeDstances_ssbo->WriteDataAsync(cmd, dirLightCascadeDistances.data(), dirLightCascadeDistances.size() * sizeof(float));
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
		bool castShadow = light->GetCastShadow();

		std::array<DynamicViewport, 6> viewports;
		if (castShadow && !GetCubeViewPorts(viewports, info, *atlasShadowMap))
			continue;

		PointLightMetaInfo metainfo;
		metainfo.position = light->GetPosition();
		metainfo.color = light->GetColor();
		metainfo.castShadow = castShadow;
		metainfo.cdIntensity = light->GetIntensity();
		metainfo.radius = light->GetRadius();
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
		bool castShadow = light->GetCastShadow();

		AtlasMap::AtlasRect rect;
		if (castShadow && (!info->atlas || !atlasShadowMap->GetSpace(info->atlas->id, rect)))
			continue;

		SpotLightMetaInfo metainfo;
		metainfo.position = light->GetPosition();
		metainfo.color = light->GetColor();
		metainfo.cdIntensity = light->GetIntensity();
		metainfo.castShadow = castShadow;
		metainfo.direction = light->GetDirection();
		metainfo.cutOff = glm::cos(glm::radians(light->GetCutOffAngle()));
		metainfo.outerCutOff = glm::cos(glm::radians(light->GetOuterCutOffAngle()));
		metainfo.lightSpaceMatrix = light->GetLightSpaceMatrix();
		metainfo.radius = light->GetRadius();
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
	uint32_t cascadeCount,
	float lambda = 0.65f // lambda控制对数/均匀混合，0.5常用
)
{
	if (cascadeCount <= 0)
		return {};

	std::vector<CascadeSplit> splits;
	splits.reserve(cascadeCount);

	for (uint32_t i = 0; i < cascadeCount; ++i) {
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
			.AddUnifromBuffer(3)
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
			.AddUnifromBuffer(3)
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
			.AddUnifromBuffer(3)
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
			.AddUnifromBuffer(3)
			.AddStorageBuffer(4)
			.AddStorageBuffer(5)
			.AddStorageBuffer(6);

		config.AddDynamicState(vk::DynamicState::eCullMode);

		if (config.Validate())
			_pointLightShadowDepthSkinnedShader->Create(config);
	}

	{
		_frustumCullingShader = std::make_shared<ComputePipeline>();

		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X * GlobalConfig::Global_WorkSize_Y);
		config.computePath = "shader/lighting/FrustumCulling.comp";

		config
			.AddUnifromBuffer(0)
			.AddStorageBuffer(1)
			.AddStorageBuffer(2)
			.AddStorageBuffer(3);

		if (config.Validate())
			_frustumCullingShader->Create(config);
	}

}

LightShadowDepthPass::~LightShadowDepthPass()
{}

void LightShadowDepthPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (state.lights.dirLightInfos.empty()
		&& state.lights.spotLightInfos.empty()
		&& state.lights.pointLightInfos.empty()
		&& !state.indirectCommands.staticMesh_OneSideCommand.empty()
		&& !state.indirectCommands.staticMesh_TwoSideCommand.empty())
		return;

	auto& selfctx = *registry.Get<SelfContext>("context");
	state.lights.shadowAtlas = selfctx.atlas;

	CalculateShadowAtlas(state, *selfctx.atlas);
	auto cmd = VKCONTEXT->GetCommandBuffer();
	SetupLightingData(selfctx, cmd, state);

	selfctx.oneSideCommandSize = state.indirectCommands.staticMesh_OneSideCommand.size();
	selfctx.twoSideCommandSize = state.indirectCommands.staticMesh_TwoSideCommand.size();
	selfctx.ssbo_StaticMesh_TransformAndMaterialIndices = state.indirectCommands.ssbo_StaticMesh_TransformAndMaterialIndices;
	selfctx.ssbo_StaticMesh_WorldAABB = state.indirectCommands.ssbo_StaticMesh_WorldAABB;

	selfctx.oneSideIndirectCommandBuffer->WriteDataAsync(cmd, state.indirectCommands.staticMesh_OneSideCommand.data(), state.indirectCommands.staticMesh_OneSideCommand.size() * sizeof(IndirectDrawCommand));
	selfctx.twoSideIndirectCommandBuffer->WriteDataAsync(cmd, state.indirectCommands.staticMesh_TwoSideCommand.data(), state.indirectCommands.staticMesh_TwoSideCommand.size() * sizeof(IndirectDrawCommand));

	if (cmd->IsRecording())
		cmd->SubmitNowAndWait();

	state.lights.ssbo_dirLightMeta = selfctx.ssbo_dirLightMeta;
	state.lights.ssbo_dirLightCascade = selfctx.ssbo_dirLightCascade;
	state.lights.ssbo_dirLightCascadeDistances = selfctx.ssbo_dirLightCascadeDistances;
	state.lights.ssbo_pointLightMeta = selfctx.ssbo_pointLightMeta;
	state.lights.ssbo_spotLightMeta = selfctx.ssbo_spotLightMeta;

	auto& datas = *registry.Get<std::vector<PreRecordData>>("PreRecordData");
	PreRecordDirAndSpotLight(datas, selfctx, state);
	PreRecordPointLight(datas, selfctx, state);
}

void LightShadowDepthPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{

	auto shadowAtlas = ctx.GetOutput(0);
	if (!shadowAtlas)
		return;

	if (state.lights.dirLightInfos.empty()
		&& state.lights.spotLightInfos.empty()
		&& state.lights.pointLightInfos.empty()
		&& !state.indirectCommands.staticMesh_OneSideCommand.empty()
		&& !state.indirectCommands.staticMesh_TwoSideCommand.empty())
		return;

	auto& selfctx = *registry.Get<SelfContext>("context");

	auto cmd = cmdCtx.GetCmd();

	glm::u32vec2 size = glm::max(selfctx.atlas->GetSize(), glm::u32vec2(16, 16));
	shadowAtlas->Resize(size.x, size.y);
	shadowAtlas->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);

	if (cmd->IsRecording())
		cmd->SubmitToQueue();

	DynamicRenderInfo renderInfo;
	renderInfo
		.SetRenderArea(shadowAtlas->GetWidth(), shadowAtlas->GetHeight())
		.AddDepthAttachment(shadowAtlas->GetImageView(vk::ImageAspectFlagBits::eDepth), vk::AttachmentLoadOp::eLoad);

	//processDirAndSpotLight(selfctx, state, cmd, renderInfo);
	//processPointLight(selfctx, state, cmd, renderInfo);

	GraphicsBindingRecord dirLightShadowDepthStaticMeshBinding;
	GraphicsBindingRecord dirLightShadowDepthSkinnedBinding;
	GraphicsBindingRecord pointLightShadowDepthStaticMeshBinding;
	GraphicsBindingRecord pointLightShadowDepthSkinnedBinding;

	dirLightShadowDepthStaticMeshBinding.SetStorageBlock(state.indirectCommands.ssbo_StaticMesh_TransformAndMaterialIndices, 5);
	dirLightShadowDepthStaticMeshBinding.SetBindlessMaterialTexture(IndirectDrawManager::Instance()->GetMaterialSSBO(), BindlessTextureManager::Instance());

	pointLightShadowDepthStaticMeshBinding.SetStorageBlock(state.indirectCommands.ssbo_StaticMesh_TransformAndMaterialIndices, 5);
	pointLightShadowDepthStaticMeshBinding.SetBindlessMaterialTexture(IndirectDrawManager::Instance()->GetMaterialSSBO(), BindlessTextureManager::Instance());

	auto& datas = *registry.Get<std::vector<PreRecordData>>("PreRecordData");
	for (auto& data : datas)
	{
		if (data.type == PreRecordLightType::Dir)
		{
			dirLightShadowDepthStaticMeshBinding.SetStorageBlock(data.ssbo_ShadowMatrices, 4);
			dirLightShadowDepthSkinnedBinding.SetStorageBlock(data.ssbo_ShadowMatrices, 4);

			PreRecordRenderScene(cmd, selfctx, data, renderInfo,
				_dirLightShadowDepthStaticMeshShader, _dirLightShadowDepthSkinnedShader,
				dirLightShadowDepthStaticMeshBinding, dirLightShadowDepthSkinnedBinding
			);
			if (cmd->IsRecording())
				cmd->SubmitNow();
		}
		else if (data.type == PreRecordLightType::Point)
		{
			pointLightShadowDepthStaticMeshBinding.SetStorageBlock(data.ssbo_ShadowMatrices, 4);
			pointLightShadowDepthStaticMeshBinding.SetStorageBlock(data.ssbo_LightProps, 6);
			pointLightShadowDepthSkinnedBinding.SetStorageBlock(data.ssbo_ShadowMatrices, 4);
			pointLightShadowDepthSkinnedBinding.SetStorageBlock(data.ssbo_LightProps, 6);

			PreRecordRenderScene(cmd, selfctx, data, renderInfo,
				_pointLightShadowDepthStaticMeshShader, _pointLightShadowDepthSkinnedShader,
				pointLightShadowDepthStaticMeshBinding, pointLightShadowDepthSkinnedBinding
			);
			if (cmd->IsRecording())
				cmd->SubmitNow();
		}
	}

}

void LightShadowDepthPass::CalculateShadowAtlas(RenderState& state, AtlasMap& atlas)
{
	auto& dirLightInfos = state.lights.dirLightInfos;
	auto& spotLightInfos = state.lights.spotLightInfos;
	auto& pointLightInfos = state.lights.pointLightInfos;

	for (auto& info : dirLightInfos)
	{
		if (!info || !info->light || !info->light->GetCastShadow())continue;
		auto& light = info->light;
		uint32_t cascadeLevel = light->GetCascadeLevel();
		uint32_t maxSize = light->GetShadowMapSize();
		for (uint32_t i = 0; i < cascadeLevel; i++)
		{
			uint32_t size = std::max(1u, uint32_t((float(cascadeLevel - i) / float(cascadeLevel)) * maxSize));
			uint32_t id;
			if (atlas.AllocateSpace(size, size, id))
				info->cascades.push_back({ id ,0 });
			else
				break;
		}

		float aspect = fabs(state.camera.projection[1][1] / state.camera.projection[0][0]);
		auto cascadeSplit = CalculateCascadeSplit(state.camera.fov, aspect, state.camera.nearPlane, state.camera.farPlane, info->cascades.size());

		for (int i = 0; i < cascadeSplit.size(); i++)
		{
			info->cascades[i].lightSpaceMatrix = info->light->GetLightSpaceMatrixWithFrustumCorners(cascadeSplit[i].projection, state.camera.view, &info->cascades[i].center, &info->cascades[i].radius);
			info->cascades[i].cascadePlaneDistance = cascadeSplit[i].farPlane;
		}
	}

	for (auto& info : spotLightInfos)
	{
		if (!info || !info->light || !info->light->GetCastShadow())continue;
		auto& light = info->light;
		uint32_t id;
		if (atlas.AllocateSpace(light->GetShadowMapSize(), light->GetShadowMapSize(), id))
		{
			info->atlas = std::make_shared<SpotLightInfo::Atlas>();
			info->atlas->id = id;
		}
	}

	for (auto& info : pointLightInfos)
	{
		if (!info || !info->light || !info->light->GetCastShadow())continue;
		auto& light = info->light;
		uint32_t id;

		info->atlas = std::make_shared<PointLightInfo::Atlas>();
		for (int i = 0; i < 6; i++)
		{
			if (atlas.AllocateSpace(light->GetShadowMapSize(), light->GetShadowMapSize(), info->atlas->ids[i]))
				info->atlas->enable[i] = true;
		}
	}

}

struct FrustumCullingParams {
	uint32_t commandCount = 0;
	uint32_t commandsPerView = 0;
	uint32_t baseAABBIndex = 0;
	FrustumCullingParams(uint32_t commandCount, uint32_t commandsPerView, uint32_t baseAABBIndex)
		:commandCount(commandCount), commandsPerView(commandsPerView), baseAABBIndex(baseAABBIndex) {}
};

struct DrawParams {
	uint32_t commandsPerView = 0;
};

void LightShadowDepthPass::SetupLightingData(SelfContext& ctx, std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state)
{
	SetupDirLightData(cmd, ctx.ssbo_dirLightMeta, ctx.ssbo_dirLightCascade, ctx.ssbo_dirLightCascadeDistances, state.lights.dirLightInfos, ctx.atlas);
	SetupPointLightData(cmd, ctx.ssbo_pointLightMeta, state.lights.pointLightInfos, ctx.atlas);
	SetupSpotLightData(cmd, ctx.ssbo_spotLightMeta, state.lights.spotLightInfos, ctx.atlas);
}

void LightShadowDepthPass::PreRecordDirAndSpotLight(std::vector<PreRecordData>& datas, SelfContext& ctx, RenderState& state)
{
	auto& dirLightsInfo = state.lights.dirLightInfos;
	auto& spotLightsInfo = state.lights.spotLightInfos;

	if (dirLightsInfo.empty() && spotLightsInfo.empty())
		return;

	std::vector<std::shared_ptr<VKWrapper::VKCommandBuffer>> cullingCmds;
	std::vector<std::shared_ptr<VKWrapper::VKFence>> cullingSyncs;

	PreRecordData data(PreRecordLightType::Dir);

	std::vector<bool> shouldCullings;

	data.shadowMatrices.reserve(batch_max);
	data.viewPorts.reserve(batch_max);
	shouldCullings.reserve(batch_max);

	uint32_t count = 0;

	auto render = [&]()->void {
		auto cmd = VKCONTEXT->GetCommandBuffer();
		auto fence = std::make_shared<VKWrapper::VKFence>(VKCONTEXT->GetDevice().get());

		data.ssbo_ShadowMatrices->WriteDataAsync(cmd, data.shadowMatrices.data(), data.shadowMatrices.size() * sizeof(glm::mat4));

		PreRecordCulling(cmd, ctx, data, shouldCullings);
		cmd->SubmitNow({}, fence);

		cullingCmds.push_back(std::move(cmd));
		cullingSyncs.push_back(std::move(fence));
		datas.push_back(std::move(data));

		data.shadowMatrices.reserve(batch_max);
		data.viewPorts.reserve(batch_max);
		count = 0;
		shouldCullings.clear();
		};

	for (auto& info : dirLightsInfo)
	{
		if (!info || !info->light || info->cascades.empty() || !info->light->GetCastShadow())
			continue;

		for (auto& cascade : info->cascades)
		{
			AtlasMap::AtlasRect rect;
			if (!ctx.atlas->GetSpace(cascade.id, rect))
				continue;

			data.shadowMatrices.push_back(cascade.lightSpaceMatrix);
			data.viewPorts.push_back({ rect.width, rect.height, rect.x ,rect.y });
			shouldCullings.push_back(false);

			count++;
			if (count >= batch_max)
				render();
		}
	}

	for (auto& info : spotLightsInfo)
	{
		if (!info || !info->light || !info->atlas || !info->light->GetCastShadow())
			continue;

		auto& light = info->light;

		if (!state.camera.frustum.IsSphereOnFrustum(light->GetPosition(), light->GetRadius()))
			continue;

		auto mat = light->GetLightSpaceMatrix();
		Frustum lightFrustm(mat);
		if (!state.camera.frustum.IsIntersectsFrustum(lightFrustm))
			continue;

		AtlasMap::AtlasRect rect;
		if (!ctx.atlas->GetSpace(info->atlas->id, rect))
			continue;

		data.shadowMatrices.push_back(mat);
		data.viewPorts.push_back({ rect.width, rect.height, rect.x ,rect.y });
		shouldCullings.push_back(true);

		count++;
		if (count >= batch_max)
			render();
	}

	if (count > 0)
		render();

	for (auto& fence : cullingSyncs)
		fence->Wait();
}

void LightShadowDepthPass::PreRecordPointLight(std::vector<PreRecordData>& datas, SelfContext& ctx, RenderState& state)
{
	auto& pointLightsInfo = state.lights.pointLightInfos;

	if (pointLightsInfo.empty())
		return;

	std::vector<std::shared_ptr<VKWrapper::VKCommandBuffer>> cullingCmds;
	std::vector<std::shared_ptr<VKWrapper::VKFence>> cullingSyncs;

	PreRecordData data(PreRecordLightType::Point);

	std::vector<bool> shouldCullings;
	std::vector<LightProp> lightProps;

	data.shadowMatrices.reserve(batch_max);
	data.viewPorts.reserve(batch_max);
	shouldCullings.resize(batch_max, true);
	lightProps.reserve(batch_max);

	uint32_t count = 0;

	auto render = [&]()->void {
		auto cmd = VKCONTEXT->GetCommandBuffer();
		auto fence = std::make_shared<VKWrapper::VKFence>(VKCONTEXT->GetDevice().get());

		data.ssbo_ShadowMatrices->WriteDataAsync(cmd, data.shadowMatrices.data(), data.shadowMatrices.size() * sizeof(glm::mat4));
		data.ssbo_LightProps->WriteDataAsync(cmd, lightProps.data(), lightProps.size() * sizeof(LightProp));

		PreRecordCulling(cmd, ctx, data, shouldCullings);
		cmd->SubmitNow({}, fence);

		cullingCmds.push_back(std::move(cmd));
		cullingSyncs.push_back(std::move(fence));
		datas.push_back(std::move(data));

		data.shadowMatrices.reserve(batch_max);
		data.viewPorts.reserve(batch_max);
		count = 0;
		lightProps.clear();
		};

	for (auto& info : pointLightsInfo)
	{
		if (!info || !info->light || !info->light->GetCastShadow())
			continue;

		std::array<DynamicViewport, 6> viewports;
		if (!GetCubeViewPorts(viewports, info, *ctx.atlas))
			continue;

		auto light = info->light;

		if (!state.camera.frustum.IsSphereOnFrustum(light->GetPosition(), light->GetRadius()))
			continue;

		float shadowMapSize = light->GetShadowMapSize();

		float near_plane = 0.1f;
		float far_plane = light->GetRadius();
		glm::mat4 shadowProj = vkPerspective(glm::radians(90.0f), 1.0, near_plane, far_plane);
		glm::vec3 lightPos = light->GetPosition();

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

			data.shadowMatrices.push_back(shadowTransforms[face]);
			lightProps.push_back({ lightPos ,far_plane });
			data.viewPorts.push_back(viewports[face]);

			count++;
			if (count >= batch_max)
				render();
		}
	}

	if (count > 0)
		render();

	for (auto& fence : cullingSyncs)
		fence->Wait();
}

void LightShadowDepthPass::PreRecordCulling(
	const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
	SelfContext& ctx,
	PreRecordData& data,
	std::vector<bool>& shouldCullings
)
{
	auto& shadowMatrices = data.shadowMatrices;
	if (shadowMatrices.empty())
		return;

	bool renderStaticMesh = ctx.oneSideCommandSize > 0 || ctx.twoSideCommandSize > 0;

	const uint32_t fustumWorksize = GlobalConfig::Global_WorkSize_X * GlobalConfig::Global_WorkSize_Y;

	static bool skipCulling = false;
	static bool skipRender = false;
	static bool skipAll = false;

	if (skipAll)
		return;

	if (false)
	{
		skipCulling = true;
		skipRender = true;
	}

	if (renderStaticMesh)
	{
		std::shared_ptr<IndirectBufferBlock>& oneSideInidrectCommands = data.oneSideInidrectCommands;
		std::shared_ptr<IndirectBufferBlock>& twoSideInidrectCommands = data.twoSideInidrectCommands;

		auto ssbo_Frustums = std::make_shared<StorageBlock>(shadowMatrices.size() * sizeof(Frustum));
		std::vector<Frustum> frustums;
		frustums.reserve(shadowMatrices.size());
		for (auto& mat : shadowMatrices)
			frustums.push_back(std::move(Frustum(mat)));
		ssbo_Frustums->WriteDataAsync(cmd, frustums.data(), frustums.size() * sizeof(Frustum));
		ssbo_Frustums->Barrier(cmd, BufferUsage::TransferWrite);

		if (ctx.oneSideCommandSize > 0)
		{
			oneSideInidrectCommands = std::make_shared<IndirectBufferBlock>(shadowMatrices.size() * ctx.oneSideCommandSize * sizeof(IndirectDrawCommand));

			FrustumCullingParams params(shadowMatrices.size() * ctx.oneSideCommandSize, ctx.oneSideCommandSize, 0);
			auto paramsUBO = std::make_shared<UniformBlock>(sizeof(FrustumCullingParams));
			paramsUBO->WriteDataAsync(cmd, &params, sizeof(params));
			paramsUBO->Barrier(cmd, BufferUsage::TransferWrite);

			for (uint32_t i = 0; i < shadowMatrices.size(); i++)
			{
				DynamicBlock::MemCopyAsync(cmd,
					ctx.oneSideIndirectCommandBuffer,
					oneSideInidrectCommands,
					ctx.oneSideCommandSize * sizeof(IndirectDrawCommand),
					0,
					i * ctx.oneSideCommandSize * sizeof(IndirectDrawCommand)
				);
			}

			if (!skipCulling)
			{
				oneSideInidrectCommands->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageWrite);

				ComputeBindingRecord fustumCullingBinding;
				fustumCullingBinding.SetUniformBlock(paramsUBO, 0);
				fustumCullingBinding.SetStorageBlock(oneSideInidrectCommands, 1);
				fustumCullingBinding.SetStorageBlock(ctx.ssbo_StaticMesh_WorldAABB, 2);
				fustumCullingBinding.SetStorageBlock(ssbo_Frustums, 3);

				_frustumCullingShader->Bind(cmd, fustumCullingBinding);
				cmd->dispatch((params.commandCount + fustumWorksize - 1) / fustumWorksize, 1, 1);
			}

		}
		if (ctx.twoSideCommandSize > 0)
		{
			twoSideInidrectCommands = std::make_shared<IndirectBufferBlock>(shadowMatrices.size() * ctx.twoSideCommandSize * sizeof(IndirectDrawCommand));

			FrustumCullingParams params(shadowMatrices.size() * ctx.twoSideCommandSize, ctx.twoSideCommandSize, ctx.oneSideCommandSize);
			auto paramsUBO = std::make_shared<UniformBlock>(sizeof(FrustumCullingParams));
			paramsUBO->WriteDataAsync(cmd, &params, sizeof(params));
			paramsUBO->Barrier(cmd, BufferUsage::TransferWrite);

			for (uint32_t i = 0; i < shadowMatrices.size(); i++)
			{
				DynamicBlock::MemCopyAsync(cmd,
					ctx.twoSideIndirectCommandBuffer,
					twoSideInidrectCommands,
					ctx.twoSideCommandSize * sizeof(IndirectDrawCommand),
					0,
					i * ctx.twoSideCommandSize * sizeof(IndirectDrawCommand)
				);
			}

			if (!skipCulling)
			{
				twoSideInidrectCommands->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageWrite);

				ComputeBindingRecord fustumCullingBinding;
				fustumCullingBinding.SetUniformBlock(paramsUBO, 0);
				fustumCullingBinding.SetStorageBlock(twoSideInidrectCommands, 1);
				fustumCullingBinding.SetStorageBlock(ctx.ssbo_StaticMesh_WorldAABB, 2);
				fustumCullingBinding.SetStorageBlock(ssbo_Frustums, 3);

				_frustumCullingShader->Bind(cmd, fustumCullingBinding);
				cmd->dispatch((params.commandCount + fustumWorksize - 1) / fustumWorksize, 1, 1);
			}
		}
	}
}

void LightShadowDepthPass::PreRecordRenderScene(
	const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
	SelfContext& ctx,
	PreRecordData& data,
	DynamicRenderInfo& renderInfo,
	std::shared_ptr<GraphicsPipeline>& shader_StaticMesh,
	std::shared_ptr<GraphicsPipeline>& shader_Skinned,
	GraphicsBindingRecord& shader_StaticMesh_Binding,
	GraphicsBindingRecord& shader_Skinned_Binding
)
{
	std::shared_ptr<UniformBlock> sideParamsUBO = std::make_shared<UniformBlock>(sizeof(DrawParams));

	auto& viewPorts = data.viewPorts;

	std::vector<vk::ClearRect> clearRects;
	clearRects.reserve(viewPorts.size());
	for (const auto& vp : viewPorts)
	{
		vk::ClearRect rect;
		rect.rect = vk::Rect2D{
			vk::Offset2D{
				static_cast<int32_t>(vp.x),
				static_cast<int32_t>(vp.y)
			},
			vk::Extent2D{
				static_cast<uint32_t>(vp.width),
				static_cast<uint32_t>(vp.height)
			}
		};

		rect.baseArrayLayer = 0;
		rect.layerCount = 1;

		clearRects.push_back(rect);
	}

	static vk::ClearAttachment clearAttachment;
	clearAttachment
		.setAspectMask(vk::ImageAspectFlagBits::eDepth)
		.setClearValue(
			vk::ClearValue{
				vk::ClearDepthStencilValue{1.0f, 0}
			});

	bool hasClear = false;

	shader_StaticMesh_Binding.SetUniformBlock(sideParamsUBO, 3);
	shader_StaticMesh->Bind(cmd, shader_StaticMesh_Binding);

	auto manager = IndirectDrawManager::Instance();

	if (ctx.oneSideCommandSize > 0)
	{
		DrawParams sideParams{ .commandsPerView = ctx.oneSideCommandSize };
		sideParamsUBO->WriteDataAsync(cmd, &sideParams, sizeof(sideParams));
		sideParamsUBO->Barrier(cmd, BufferUsage::TransferWrite);

		cmd->beginRendering(renderInfo);
		cmd->setDynamicViewports(viewPorts);
		cmd->setCullMode(vk::CullModeFlagBits::eNone);

		if (!hasClear)
		{
			cmd->clearAttachments(clearAttachment, clearRects);
			hasClear = true;
		}

		cmd->bindVertexBuffers(manager->GetVertexBlock());
		cmd->bindIndexBuffer(manager->GetIndexBlock());
		cmd->drawIndexedIndirect(data.oneSideInidrectCommands, data.shadowMatrices.size() * ctx.oneSideCommandSize);
		cmd->endRendering();

		if (ctx.twoSideCommandSize > 0)
			sideParamsUBO->Barrier(cmd, BufferUsage::UniformRead);
	}

	if (ctx.twoSideCommandSize > 0)
	{
		DrawParams sideParams{ .commandsPerView = ctx.twoSideCommandSize };
		sideParamsUBO->WriteDataAsync(cmd, &sideParams, sizeof(sideParams));
		sideParamsUBO->Barrier(cmd, BufferUsage::TransferWrite);

		cmd->beginRendering(renderInfo);
		cmd->setDynamicViewports(viewPorts);
		cmd->setCullMode(vk::CullModeFlagBits::eNone);

		if (!hasClear)
		{
			cmd->clearAttachments(clearAttachment, clearRects);
			hasClear = true;
		}

		cmd->bindVertexBuffers(manager->GetVertexBlock());
		cmd->bindIndexBuffer(manager->GetIndexBlock());
		cmd->drawIndexedIndirect(data.twoSideInidrectCommands, data.shadowMatrices.size() * ctx.twoSideCommandSize);
		cmd->endRendering();
	}

}
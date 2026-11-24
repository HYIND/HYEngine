#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/LightShadowDepthPass.h"
#include "VulkanRenderEngine/General/RenderHelper.h"

constexpr uint32_t batch_max = 16;

struct alignas(16) DirLightMetaInfo {
	alignas(16) glm::vec3 direction;
	alignas(16) glm::vec3 color;

	float luxIntensity;

	uint32_t castShadow;
	int cascadeFirst;
	int cascadeCount;

	uint32_t volumeLightEnable = 0;
	uint32_t volumeLightEffectByAtmospher = 0;
	float volumetricScatteringIntensity;

	// 阴影距离渐隐区间（相机视空间深度，米）：深度 <= start 完整阴影，>= end 不算阴影，中间渐隐。
	// 阴影距离没有被相机远平面截断时两者都设成极大值，即不渐隐（远平面以外本来就看不到）。
	// 和 lightingssboDef.comp 的 DirLight 对齐：volumetricScatteringIntensity 在 52，这两个在 56 / 60，结构体仍是 64 字节
	float shadowFadeStart = 3.0e38f;
	float shadowFadeEnd = 3.0e38f;

	// 法线偏移（texel），偏移 64；结构体按 16 对齐到 80 字节，和 GLSL std430 一致
	float shadowNormalBias = 1.0f;

	// 阴影调试：这盏灯就是编辑器里选中的那盏（偏移 68，结构体仍是 80 字节）
	uint32_t shadowDebug = 0;
};
static_assert(sizeof(DirLightMetaInfo) == 80);
static_assert(offsetof(DirLightMetaInfo, shadowNormalBias) == 64);
static_assert(offsetof(DirLightMetaInfo, shadowDebug) == 68);

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

	uint32_t volumeLightEnable = 0;
	uint32_t volumeLightEffectByAtmospher = 0;
	float volumetricScatteringIntensity;

	// 法线偏移（texel），占用 mat4 前原有的 4 字节空隙（偏移 92），结构体大小不变
	float shadowNormalBias = 1.0f;

	alignas(16) glm::mat4 lightSpaceMatrix;
};
static_assert(sizeof(SpotLightMetaInfo) == 160);
static_assert(offsetof(SpotLightMetaInfo, shadowNormalBias) == 92);

struct alignas(16) PointLightMetaInfo {
	alignas(16) glm::vec3 position;
	alignas(16) glm::vec3 color;
	alignas(16) glm::vec2 atlasStart[6];
	alignas(16) glm::vec2 atlasSize[6];
	uint32_t castShadow;
	float cdIntensity;
	float radius;

	uint32_t volumeLightEnable = 0;
	uint32_t volumeLightEffectByAtmospher = 0;
	float volumetricScatteringIntensity;

	// 法线偏移（texel），占用结构体末尾原有的空隙（偏移 152），结构体大小不变
	float shadowNormalBias = 1.0f;
};
static_assert(sizeof(PointLightMetaInfo) == 160);
static_assert(offsetof(PointLightMetaInfo, shadowNormalBias) == 152);

// 阴影深度偏移的常量项（× 深度格式的最小可分辨差 r，D32F 下 r ≈ 2^(e-23)），只用来吸收光栅化和 shader 里矩阵运算的浮点误差；
// 真正的偏移靠斜率项（每盏灯的 shadowSlopeBias）和接收端的法线偏移
constexpr float kShadowDepthBiasConstant = 4.0f;

// 点光源 cube 每个面的透视投影近平面，必须和 lightingHelper.comp 的 kPointShadowNearPlane 一致
constexpr float kPointShadowNearPlane = 0.1f;

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

// 阴影距离最后这一段做渐隐，避免在阴影距离处出现一条硬边
constexpr float kDirShadowFadeRatio = 0.1f;

static void SetupDirLightData(
	std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
	std::shared_ptr<StorageBlock>& meta_ssbo,
	std::shared_ptr<StorageBlock>& cascade_ssbo,
	std::shared_ptr<StorageBlock>& cascadeDstances_ssbo,
	const std::vector<std::shared_ptr<DirLightInfo>>& dirLights,
	const std::shared_ptr<AtlasMap>& atlasShadowMap,
	float cameraFarPlane,
	uint32_t shadowDebugEntity
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

		metainfo.volumeLightEnable = uint32_t(light->GetVolumeLightEnable());
		metainfo.volumeLightEffectByAtmospher = uint32_t(light->GetVolumeLightEffectByAtmospher());
		metainfo.volumetricScatteringIntensity = light->GetVolumetricScatteringIntensity();
		metainfo.shadowNormalBias = light->GetShadowNormalBias();
		// 阴影调试只对选中的那盏方向光生效（RenderLight 不可见的灯根本不会进这个列表）
		metainfo.shadowDebug = (shadowDebugEntity != UINT32_MAX && info->entityId == shadowDebugEntity) ? 1u : 0u;

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

			// 阴影距离比相机远平面近时，最后一级的远端就是阴影距离：在它前面的 10% 渐隐到无阴影
			if (!info->cascades.empty())
			{
				const float shadowFar = info->cascades.back().cascadePlaneDistance;
				if (shadowFar < cameraFarPlane)
				{
					metainfo.shadowFadeEnd = shadowFar;
					metainfo.shadowFadeStart = shadowFar * (1.0f - kDirShadowFadeRatio);
				}
			}
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
		metainfo.volumeLightEnable = uint32_t(light->GetVolumeLightEnable());
		metainfo.volumeLightEffectByAtmospher = uint32_t(light->GetVolumeLightEffectByAtmospher());
		metainfo.volumetricScatteringIntensity = light->GetVolumetricScatteringIntensity();
		metainfo.shadowNormalBias = light->GetShadowNormalBias();
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
		metainfo.volumeLightEnable = uint32_t(light->GetVolumeLightEnable());
		metainfo.volumeLightEffectByAtmospher = uint32_t(light->GetVolumeLightEffectByAtmospher());
		metainfo.volumetricScatteringIntensity = light->GetVolumetricScatteringIntensity();
		metainfo.shadowNormalBias = light->GetShadowNormalBias();
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
	float lambda = 0.75f // lambda 控制对数 / 均匀混合：0 均匀，1 对数
)
{
	if (cascadeCount <= 0)
		return {};

	lambda = std::clamp(lambda, 0.0f, 1.0f);

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
	const std::string& dirLightShadowFragmentShaderPath,
	const std::string& pointLightShadowStaticMeshVertexShaderPath,
	const std::string& pointLightShadowFragmentShaderPath
)
{

	{
		_dirLightShadowDepthStaticMeshShader = std::make_shared<GraphicsPipeline>();

		GraphicsPipelineConfig config;
		config.vertexPath = dirLightShadowStaticMeshVertexShaderPath;
		config.fragmentPath = dirLightShadowFragmentShaderPath;

		config.AddVertexInputAttributeDescription(StaticMesh::GetVertexInputAttributeDescription());
		config.AddVertexInputBindingDescription(StaticMesh::GetVertexInputBindingDescription());

		config.SetDepthAttachmentFormat(vk::Format::eD32Sfloat);
		config.SetStencilAttachmentFormat(vk::Format::eUndefined);

		config
			.AddShadowBindlessMaterialTextureBinding()
			.AddUnifromBuffer(3)
			.AddStorageBuffer(4)
			.AddStorageBuffer(5);

		config.AddDynamicState(vk::DynamicState::eCullMode);
		// 斜率深度偏移：画阴影图时硬件按三角形的深度斜率把深度推远，数值每批按灯光参数动态设置
		config.SetDepthBiasEnable(true);
		config.AddDynamicState(vk::DynamicState::eDepthBias);

		if (config.Validate())
			_dirLightShadowDepthStaticMeshShader->Create(config);
	}

	{
		_pointLightShadowDepthStaticMeshShader = std::make_shared<GraphicsPipeline>();

		GraphicsPipelineConfig config;
		config.vertexPath = pointLightShadowStaticMeshVertexShaderPath;
		config.fragmentPath = pointLightShadowFragmentShaderPath;

		config.AddVertexInputAttributeDescription(StaticMesh::GetVertexInputAttributeDescription());
		config.AddVertexInputBindingDescription(StaticMesh::GetVertexInputBindingDescription());

		config.SetDepthAttachmentFormat(vk::Format::eD32Sfloat);
		config.SetStencilAttachmentFormat(vk::Format::eUndefined);

		config
			.AddShadowBindlessMaterialTextureBinding()
			.AddUnifromBuffer(3)
			.AddStorageBuffer(4)
			.AddStorageBuffer(5)
			.AddStorageBuffer(6);

		config.AddDynamicState(vk::DynamicState::eCullMode);
		// 点光源也走硬件透视深度（FS 不再写 gl_FragDepth），所以同样可以用硬件斜率深度偏移
		config.SetDepthBiasEnable(true);
		config.AddDynamicState(vk::DynamicState::eDepthBias);

		if (config.Validate())
			_pointLightShadowDepthStaticMeshShader->Create(config);
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
	// 不做提前返回：没有灯光时也要把灯光数量 0 上传，并把 state.lights.ssbo_* 交出去。
	// LightingPass、RTR / RTGI、DDGI 都会绑定这些 SSBO 读 dirLightCount 等，指针为空时描述符不会被设置。
	// 没有灯光时下面的 PreRecord* 会直接返回，开销只有几次小的上传。
	auto& selfctx = registry.Get<SelfContext>("context");
	state.lights.shadowAtlas = selfctx.atlas;

	CalculateShadowAtlas(state, *selfctx.atlas);
	auto cmd = VKCONTEXT->GetCommandBuffer();
	SetupLightingData(selfctx, cmd, state);

	selfctx.staticOneSideCommandSize = state.indirectCommands.staticMesh_OneSideCommand.size();
	selfctx.staticTwoSideCommandSize = state.indirectCommands.staticMesh_TwoSideCommand.size();
	selfctx.skinnedOneSideCommandSize = state.indirectCommands.skinnedMesh_OneSideCommand.size();
	selfctx.skinnedTwoSideCommandSize = state.indirectCommands.skinnedMesh_TwoSideCommand.size();
	selfctx.ssbo_StaticMesh_TransformAndMaterialIndices = state.indirectCommands.ssbo_StaticMesh_TransformAndMaterialIndices;
	selfctx.ssbo_StaticMesh_WorldAABB = state.indirectCommands.ssbo_StaticMesh_WorldAABB;
	selfctx.ssbo_SkinnedMesh_TransformAndMaterialIndices = state.indirectCommands.ssbo_SkinnedMesh_TransformAndMaterialIndices;
	selfctx.skinnedMesh_VertexBuffer = (state.curFrameSkinnedData && state.curFrameSkinnedData->skinnedMesh_VertexBuffer) ? state.curFrameSkinnedData->skinnedMesh_VertexBuffer->buffer : nullptr;

	selfctx.oneSideIndirectCommandBuffer->WriteDataAsync(cmd, state.indirectCommands.staticMesh_OneSideCommand.data(), state.indirectCommands.staticMesh_OneSideCommand.size() * sizeof(IndirectDrawCommand));
	selfctx.twoSideIndirectCommandBuffer->WriteDataAsync(cmd, state.indirectCommands.staticMesh_TwoSideCommand.data(), state.indirectCommands.staticMesh_TwoSideCommand.size() * sizeof(IndirectDrawCommand));
	selfctx.skinnedOneSideIndirectCommandBuffer->WriteDataAsync(cmd, state.indirectCommands.skinnedMesh_OneSideCommand.data(), state.indirectCommands.skinnedMesh_OneSideCommand.size() * sizeof(IndirectDrawCommand));
	selfctx.skinnedTwoSideIndirectCommandBuffer->WriteDataAsync(cmd, state.indirectCommands.skinnedMesh_TwoSideCommand.data(), state.indirectCommands.skinnedMesh_TwoSideCommand.size() * sizeof(IndirectDrawCommand));

	if (cmd->IsRecording())
		cmd->SubmitNowAndWait();

	state.lights.ssbo_dirLightMeta = selfctx.ssbo_dirLightMeta;
	state.lights.ssbo_dirLightCascade = selfctx.ssbo_dirLightCascade;
	state.lights.ssbo_dirLightCascadeDistances = selfctx.ssbo_dirLightCascadeDistances;
	state.lights.ssbo_pointLightMeta = selfctx.ssbo_pointLightMeta;
	state.lights.ssbo_spotLightMeta = selfctx.ssbo_spotLightMeta;

	auto& datas = *registry.GetShared<std::vector<PreRecordData>>("PreRecordData");
	PreRecordDirAndSpotLight(datas, selfctx, state);
	PreRecordPointLight(datas, selfctx, state);
}

void LightShadowDepthPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{

	auto shadowAtlas = ctx.GetOutput(0);
	if (!shadowAtlas)
		return;

	auto& selfctx = registry.Get<SelfContext>("context");

	auto cmd = cmdCtx.GetCmd();

	glm::u32vec2 size = glm::max(selfctx.atlas->GetSize(), glm::u32vec2(16, 16));
	shadowAtlas->Resize(size.x, size.y);
	shadowAtlas->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);

	if (cmd->IsRecording())
		cmd->SubmitToQueue();

	// 图集的尺寸和布局每帧都要设置（后面的 Pass 会把它当输入读），之后没有要画的阴影视口（没有灯光）就结束
	auto& datas = *registry.GetShared<std::vector<PreRecordData>>("PreRecordData");
	if (datas.empty())
		return;

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
	dirLightShadowDepthStaticMeshBinding.SetShadowBindlessMaterialTexture(IndirectDrawManager::Instance()->GetShadowMaterialSSBO(), BindlessTextureManager::Instance());
	dirLightShadowDepthSkinnedBinding.SetStorageBlock(state.indirectCommands.ssbo_SkinnedMesh_TransformAndMaterialIndices, 5);
	dirLightShadowDepthSkinnedBinding.SetShadowBindlessMaterialTexture(IndirectDrawManager::Instance()->GetShadowMaterialSSBO(), BindlessTextureManager::Instance());

	pointLightShadowDepthStaticMeshBinding.SetStorageBlock(state.indirectCommands.ssbo_StaticMesh_TransformAndMaterialIndices, 5);
	pointLightShadowDepthStaticMeshBinding.SetShadowBindlessMaterialTexture(IndirectDrawManager::Instance()->GetShadowMaterialSSBO(), BindlessTextureManager::Instance());
	pointLightShadowDepthSkinnedBinding.SetStorageBlock(state.indirectCommands.ssbo_SkinnedMesh_TransformAndMaterialIndices, 5);
	pointLightShadowDepthSkinnedBinding.SetShadowBindlessMaterialTexture(IndirectDrawManager::Instance()->GetShadowMaterialSSBO(), BindlessTextureManager::Instance());

	for (auto& data : datas)
	{
		if (data.type == PreRecordLightType::Dir)
		{
			dirLightShadowDepthStaticMeshBinding.SetStorageBlock(data.ssbo_ShadowMatrices, 4);
			dirLightShadowDepthSkinnedBinding.SetStorageBlock(data.ssbo_ShadowMatrices, 4);

			// 动态状态不跨命令缓冲：每批（每次 SubmitNow 之后）都要重新设置
			cmd->setDepthBias(kShadowDepthBiasConstant, 0.0f, data.slopeBias);

			PreRecordRenderScene(cmd, selfctx, data, renderInfo,
				_dirLightShadowDepthStaticMeshShader,
				dirLightShadowDepthStaticMeshBinding,
				dirLightShadowDepthSkinnedBinding
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

			cmd->setDepthBias(kShadowDepthBiasConstant, 0.0f, data.slopeBias);

			PreRecordRenderScene(cmd, selfctx, data, renderInfo,
				_pointLightShadowDepthStaticMeshShader,
				pointLightShadowDepthStaticMeshBinding,
				pointLightShadowDepthSkinnedBinding
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
		uint32_t maxSize = std::max(1u, light->GetShadowMapSize());
		uint32_t halfSize = std::max(1u, maxSize / 2);
		for (uint32_t i = 0; i < cascadeLevel; i++)
		{
			uint32_t size = std::max(halfSize, uint32_t((float(cascadeLevel - i) / float(cascadeLevel)) * maxSize));
			uint32_t id;
			if (atlas.AllocateSpace(size, size, id))
				info->cascades.push_back({ id ,0 });
			else
				break;
		}

		// 阴影距离和相机远平面分开：级联只在 [near, min(far, 阴影距离)] 内切分，
		// 远平面再大也不会把近处级联拉远。超出阴影距离的部分在 shader 里按无阴影处理
		float aspect = fabs(state.camera.projection[1][1] / state.camera.projection[0][0]);
		const float nearPlane = state.camera.nearPlane;
		const float shadowFar = std::max(std::min(state.camera.farPlane, light->GetShadowDistance()), nearPlane * 2.0f);
		auto cascadeSplit = CalculateCascadeSplit(state.camera.fov, aspect, nearPlane, shadowFar,
			info->cascades.size(), light->GetCascadeSplitLambda());

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
	const uint32_t shadowDebugEntity = state.option.shadowDebugMode != Scene3DRenderOption::ShadowDebugMode::Off
		? state.option.shadowDebugEntity : UINT32_MAX;
	SetupDirLightData(cmd, ctx.ssbo_dirLightMeta, ctx.ssbo_dirLightCascade, ctx.ssbo_dirLightCascadeDistances, state.lights.dirLightInfos, ctx.atlas, state.camera.farPlane, shadowDebugEntity);
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

	// 加一个视口之前调用：斜率偏移和当前批不同就先把当前批提交（depthBias 是每次绘制一个值）
	auto beginView = [&](float slopeBias) {
		if (count > 0 && data.slopeBias != slopeBias)
			render();
		data.slopeBias = slopeBias;
		};

	for (auto& info : dirLightsInfo)
	{
		if (!info || !info->light || info->cascades.empty() || !info->light->GetCastShadow())
			continue;

		const float slopeBias = info->light->GetShadowSlopeBias();

		for (auto& cascade : info->cascades)
		{
			AtlasMap::AtlasRect rect;
			if (!ctx.atlas->GetSpace(cascade.id, rect))
				continue;

			beginView(slopeBias);
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

		beginView(light->GetShadowSlopeBias());
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

	auto beginView = [&](float slopeBias) {
		if (count > 0 && data.slopeBias != slopeBias)
			render();
		data.slopeBias = slopeBias;
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

		// 近平面和 shader 里按面算参考深度用的是同一个常量
		float near_plane = kPointShadowNearPlane;
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

			beginView(light->GetShadowSlopeBias());
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

	bool renderStaticMesh = ctx.staticOneSideCommandSize > 0 || ctx.staticTwoSideCommandSize > 0;

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
		std::shared_ptr<IndirectBufferBlock>& staticOneSideInidrectCommands = data.staticOneSideInidrectCommands;
		std::shared_ptr<IndirectBufferBlock>& staticTwoSideInidrectCommands = data.staticTwoSideInidrectCommands;

		auto ssbo_Frustums = std::make_shared<StorageBlock>(shadowMatrices.size() * sizeof(Frustum));
		std::vector<Frustum> frustums;
		frustums.reserve(shadowMatrices.size());
		for (auto& mat : shadowMatrices)
			frustums.push_back(std::move(Frustum(mat)));
		ssbo_Frustums->WriteDataAsync(cmd, frustums.data(), frustums.size() * sizeof(Frustum));
		ssbo_Frustums->Barrier(cmd, BufferUsage::TransferWrite);

		if (ctx.staticOneSideCommandSize > 0)
		{
			staticOneSideInidrectCommands = std::make_shared<IndirectBufferBlock>(shadowMatrices.size() * ctx.staticOneSideCommandSize * sizeof(IndirectDrawCommand));

			FrustumCullingParams params(shadowMatrices.size() * ctx.staticOneSideCommandSize, ctx.staticOneSideCommandSize, 0);
			auto paramsUBO = std::make_shared<UniformBlock>(sizeof(FrustumCullingParams));
			paramsUBO->WriteDataAsync(cmd, &params, sizeof(params));
			paramsUBO->Barrier(cmd, BufferUsage::TransferWrite);

			for (uint32_t i = 0; i < shadowMatrices.size(); i++)
			{
				DynamicBlock::MemCopyAsync(cmd,
					ctx.oneSideIndirectCommandBuffer,
					staticOneSideInidrectCommands,
					ctx.staticOneSideCommandSize * sizeof(IndirectDrawCommand),
					0,
					i * ctx.staticOneSideCommandSize * sizeof(IndirectDrawCommand)
				);
			}

			if (!skipCulling)
			{
				staticOneSideInidrectCommands->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageWrite);

				ComputeBindingRecord fustumCullingBinding;
				fustumCullingBinding.SetUniformBlock(paramsUBO, 0);
				fustumCullingBinding.SetStorageBlock(staticOneSideInidrectCommands, 1);
				fustumCullingBinding.SetStorageBlock(ctx.ssbo_StaticMesh_WorldAABB, 2);
				fustumCullingBinding.SetStorageBlock(ssbo_Frustums, 3);

				_frustumCullingShader->Bind(cmd, fustumCullingBinding);
				cmd->dispatch((params.commandCount + fustumWorksize - 1) / fustumWorksize, 1, 1);
			}

		}
		if (ctx.staticTwoSideCommandSize > 0)
		{
			staticTwoSideInidrectCommands = std::make_shared<IndirectBufferBlock>(shadowMatrices.size() * ctx.staticTwoSideCommandSize * sizeof(IndirectDrawCommand));

			FrustumCullingParams params(shadowMatrices.size() * ctx.staticTwoSideCommandSize, ctx.staticTwoSideCommandSize, ctx.staticOneSideCommandSize);
			auto paramsUBO = std::make_shared<UniformBlock>(sizeof(FrustumCullingParams));
			paramsUBO->WriteDataAsync(cmd, &params, sizeof(params));
			paramsUBO->Barrier(cmd, BufferUsage::TransferWrite);

			for (uint32_t i = 0; i < shadowMatrices.size(); i++)
			{
				DynamicBlock::MemCopyAsync(cmd,
					ctx.twoSideIndirectCommandBuffer,
					staticTwoSideInidrectCommands,
					ctx.staticTwoSideCommandSize * sizeof(IndirectDrawCommand),
					0,
					i * ctx.staticTwoSideCommandSize * sizeof(IndirectDrawCommand)
				);
			}

			if (!skipCulling)
			{
				staticTwoSideInidrectCommands->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageWrite);

				ComputeBindingRecord fustumCullingBinding;
				fustumCullingBinding.SetUniformBlock(paramsUBO, 0);
				fustumCullingBinding.SetStorageBlock(staticTwoSideInidrectCommands, 1);
				fustumCullingBinding.SetStorageBlock(ctx.ssbo_StaticMesh_WorldAABB, 2);
				fustumCullingBinding.SetStorageBlock(ssbo_Frustums, 3);

				_frustumCullingShader->Bind(cmd, fustumCullingBinding);
				cmd->dispatch((params.commandCount + fustumWorksize - 1) / fustumWorksize, 1, 1);
			}
		}
	}

	if (ctx.skinnedOneSideCommandSize > 0)
	{
		const uint32_t commandBytes = ctx.skinnedOneSideCommandSize * sizeof(IndirectDrawCommand);
		data.skinnedOneSideIndirectCommands = std::make_shared<IndirectBufferBlock>(shadowMatrices.size() * commandBytes);
		for (uint32_t viewIndex = 0; viewIndex < shadowMatrices.size(); ++viewIndex)
		{
			DynamicBlock::MemCopyAsync(cmd,
				ctx.skinnedOneSideIndirectCommandBuffer,
				data.skinnedOneSideIndirectCommands,
				commandBytes,
				0,
				viewIndex * commandBytes);
		}
		data.skinnedOneSideIndirectCommands->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::IndirectRead);
	}

	if (ctx.skinnedTwoSideCommandSize > 0)
	{
		const uint32_t commandBytes = ctx.skinnedTwoSideCommandSize * sizeof(IndirectDrawCommand);
		data.skinnedTwoSideIndirectCommands = std::make_shared<IndirectBufferBlock>(shadowMatrices.size() * commandBytes);
		for (uint32_t viewIndex = 0; viewIndex < shadowMatrices.size(); ++viewIndex)
		{
			DynamicBlock::MemCopyAsync(cmd,
				ctx.skinnedTwoSideIndirectCommandBuffer,
				data.skinnedTwoSideIndirectCommands,
				commandBytes,
				0,
				viewIndex * commandBytes);
		}
		data.skinnedTwoSideIndirectCommands->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::IndirectRead);
	}
}

void LightShadowDepthPass::PreRecordRenderScene(
	const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
	SelfContext& ctx,
	PreRecordData& data,
	DynamicRenderInfo& renderInfo,
	std::shared_ptr<GraphicsPipeline>& shader_StaticMesh,
	GraphicsBindingRecord& shader_StaticMesh_Binding,
	GraphicsBindingRecord& shader_SkinnedMesh_Binding
)
{
	std::shared_ptr<UniformBlock> sideParamsUBO = std::make_shared<UniformBlock>(sizeof(DrawParams));
	std::shared_ptr<UniformBlock> skinnedSideParamsUBO = std::make_shared<UniformBlock>(sizeof(DrawParams));

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


	auto manager = IndirectDrawManager::Instance();

	auto renderStaticCommands = [&](uint32_t commandSize, const std::shared_ptr<IndirectBufferBlock>& indirectCommands, vk::CullModeFlags cullMode)
		{
			if (commandSize == 0 || !indirectCommands)
				return;

			DrawParams sideParams{ .commandsPerView = commandSize };
			sideParamsUBO->WriteDataAsync(cmd, &sideParams, sizeof(sideParams));
			sideParamsUBO->Barrier(cmd, BufferUsage::TransferWrite);
			shader_StaticMesh_Binding.SetUniformBlock(sideParamsUBO, 3);
			shader_StaticMesh->Bind(cmd, shader_StaticMesh_Binding);

			cmd->beginRendering(renderInfo);
			cmd->setDynamicViewports(viewPorts);
			cmd->setCullMode(cullMode);

			if (!hasClear)
			{
				cmd->clearAttachments(clearAttachment, clearRects);
				hasClear = true;
			}

			cmd->bindVertexBuffers(manager->GetVertexBlock());
			cmd->bindIndexBuffer(manager->GetIndexBlock());
			cmd->drawIndexedIndirect(indirectCommands, data.shadowMatrices.size() * commandSize);
			cmd->endRendering();
		};

	if (ctx.staticOneSideCommandSize > 0)
	{
		renderStaticCommands(ctx.staticOneSideCommandSize, data.staticOneSideInidrectCommands, vk::CullModeFlagBits::eNone);
		if (ctx.staticTwoSideCommandSize > 0)
			sideParamsUBO->Barrier(cmd, BufferUsage::UniformRead);
	}

	if (ctx.staticTwoSideCommandSize > 0)
	{
		renderStaticCommands(ctx.staticTwoSideCommandSize, data.staticTwoSideInidrectCommands, vk::CullModeFlagBits::eNone);
	}

	auto renderSkinnedCommands = [&](uint32_t commandSize, const std::shared_ptr<IndirectBufferBlock>& indirectCommands, vk::CullModeFlags cullMode)
		{
			if (commandSize == 0 || !indirectCommands || !ctx.skinnedMesh_VertexBuffer)
				return;

			DrawParams sideParams{ .commandsPerView = commandSize };
			skinnedSideParamsUBO->WriteDataAsync(cmd, &sideParams, sizeof(sideParams));
			skinnedSideParamsUBO->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::UniformRead);
			shader_SkinnedMesh_Binding.SetUniformBlock(skinnedSideParamsUBO, 3);
			shader_StaticMesh->Bind(cmd, shader_SkinnedMesh_Binding);

			cmd->beginRendering(renderInfo);
			cmd->setDynamicViewports(viewPorts);
			cmd->setCullMode(cullMode);
			if (!hasClear)
			{
				cmd->clearAttachments(clearAttachment, clearRects);
				hasClear = true;
			}

			cmd->bindVertexBuffers(ctx.skinnedMesh_VertexBuffer);
			cmd->bindIndexBuffer(manager->GetIndexBlock());
			cmd->drawIndexedIndirect(indirectCommands, data.shadowMatrices.size() * commandSize);
			cmd->endRendering();
		};

	if (ctx.skinnedOneSideCommandSize > 0)
	{
		renderSkinnedCommands(ctx.skinnedOneSideCommandSize, data.skinnedOneSideIndirectCommands, vk::CullModeFlagBits::eNone);
		if (ctx.skinnedTwoSideCommandSize > 0)
			skinnedSideParamsUBO->Barrier(cmd, BufferUsage::UniformRead, BufferUsage::TransferWrite);
	}
	if (ctx.skinnedTwoSideCommandSize > 0)
		renderSkinnedCommands(ctx.skinnedTwoSideCommandSize, data.skinnedTwoSideIndirectCommands, vk::CullModeFlagBits::eNone);

	// 场景里没有可投影的网格时也要清掉这些阴影视口（loadOp = eLoad），
	// 否则阴影图集保留上一帧的内容，体积光 / 透明物体会采到已删除物体的残影
	if (!hasClear && !clearRects.empty())
	{
		cmd->beginRendering(renderInfo);
		cmd->clearAttachments(clearAttachment, clearRects);
		cmd->endRendering();
	}
}


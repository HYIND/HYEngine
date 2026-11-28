#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/IBLPreCalculatePass.h"

IBLPreCalculatePass::IBLPreCalculatePass()
{
	// 辐照度 cube 很平滑，每面 32 就够；预过滤 cube 256²，6 级 mip 对应 roughness 0~1。
	constexpr uint32_t skyCubeSize = 256;
	constexpr uint32_t diffuseCubeSize = 32;
	constexpr uint32_t prefilterLevel = 6;

	_diffuse = std::make_shared<TextureCube>(diffuseCubeSize, vk::Format::eR16G16B16A16Sfloat, TextureConfig::GetDefaultSkyCubeConfig());
	_prefilter = std::make_shared<TextureCube>(skyCubeSize, vk::Format::eR16G16B16A16Sfloat, TextureConfig::GetDefaultSkyCubeConfig(), prefilterLevel);
}

IBLPreCalculatePass::SourceKind IBLPreCalculatePass::SelectSource(const RenderState& state, std::shared_ptr<TextureCube>& outCube, uint64_t& outRevision) const
{
	// 大气优先，其次天空盒。两者 ShouldExecute 互斥，这里仍按优先级写死，保证切场景时决策稳定。
	const auto& atmo = state.skyAtmosphereParams;
	if (atmo.hasSkyAtmospherePreData && atmo.skyCube && !atmo.skyCube->IsEmpty())
	{
		outCube = atmo.skyCube;
		outRevision = atmo.skyCubeRevision;
		return SourceKind::Atmosphere;
	}

	const auto& skybox = state.skyboxParams;
	if (skybox.hasSkyBoxPreData && skybox.skyCube && !skybox.skyCube->IsEmpty())
	{
		outCube = skybox.skyCube;
		outRevision = skybox.skyCubeRevision;
		return SourceKind::SkyBox;
	}

	return SourceKind::None;
}

void IBLPreCalculatePass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	std::shared_ptr<TextureCube> sourceCube;
	uint64_t revision = 0;
	const SourceKind source = SelectSource(state, sourceCube, revision);

	// 没有任何天空源：IBL 为空。消费者用 IBLReady() 判断，走无环境光的分支。
	if (source == SourceKind::None || !sourceCube)
	{
		state.iblParams = {};
		_lastSource = SourceKind::None;
		_lastCube.reset();
		_lastRevision = 0;
		return;
	}

	const bool needUpdate = source != _lastSource
		|| sourceCube != _lastCube
		|| revision != _lastRevision;

	if (needUpdate)
	{
		auto cmd = cmdCtx.GetCmd();

		{
			IBLImageBuilder::DiffuseGenerateParams diffuseParams{};
			_iblBuilder.CalculateCubeDiffuse(cmd, _diffuse, sourceCube, diffuseParams);
		}

		{
			IBLImageBuilder::PrefilterGenerateParams prefilterParams{};
			_iblBuilder.CalculateCubePrefilter(cmd, _prefilter, sourceCube, prefilterParams);
		}

		// BRDF LUT 不依赖天空图，全环境共用一张，只算一次
		if (!_brdfLUT)
		{
			_brdfLUT = std::make_shared<Texture2D>(512, 512, vk::Format::eR16G16B16A16Sfloat, TextureConfig::GetDefaultSkyCubeConfig());
			IBLImageBuilder::BRDFLUTGenerateParams lutParams{};
			_iblBuilder.CalculateBRDFLUT(cmd, _brdfLUT, lutParams);
		}

		if (cmd->IsRecording())
			cmd->SubmitToQueue();

		_lastSource = source;
		_lastCube = sourceCube;
		_lastRevision = revision;
	}

	// 无论本帧是否重算，都要把缓存的 IBL 暴露给同帧消费者
	state.iblParams.diffuse = _diffuse;
	state.iblParams.prefilter = _prefilter;
	state.iblParams.brdfLUT = _brdfLUT;
}

#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/IBLPreCalculatePass.h"

namespace
{
	// std140 bool = 4 字节；与 CloudSkyComposite.comp 的 CompositeParams 对应
	struct alignas(16) CompositeParams
	{
		uint32_t BackgroundEnable = 0;
		uint32_t Reserved0 = 0;
		uint32_t Reserved1 = 0;
		uint32_t Reserved2 = 0;
	};
}

IBLPreCalculatePass::IBLPreCalculatePass(const std::string& compositeShaderPath)
{
	// 辐照度 cube 很平滑，每面 32 就够；预过滤 cube 256²，6 级 mip 对应 roughness 0~1。
	constexpr uint32_t skyCubeSize = 256;
	constexpr uint32_t diffuseCubeSize = 32;
	constexpr uint32_t prefilterLevel = 6;

	_diffuse = std::make_shared<TextureCube>(diffuseCubeSize, vk::Format::eR16G16B16A16Sfloat, TextureConfig::GetDefaultSkyCubeConfig());
	_prefilter = std::make_shared<TextureCube>(skyCubeSize, vk::Format::eR16G16B16A16Sfloat, TextureConfig::GetDefaultSkyCubeConfig(), prefilterLevel);

	// 最终天空 cube 与大气 skyCube 同尺寸，带完整 mip；IBL 与反射都从它采样。
	constexpr uint32_t finalSkyCubeLevels = 9;	// log2(256) + 1
	_finalSkyCube = std::make_shared<TextureCube>(skyCubeSize, vk::Format::eR16G16B16A16Sfloat, TextureConfig::GetDefaultSkyCubeConfig(), finalSkyCubeLevels);
	_blackCube = std::make_shared<TextureCube>(1, vk::Format::eR16G16B16A16Sfloat, TextureConfig::GetDefaultSkyCubeConfig());

	ComputePipelineConfig config;
	config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
	config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
	config.computePath = compositeShaderPath;

	config
		.AddUnifromBuffer(0)					// CompositeParams
		.AddUnifromTexture(1)					// cloud cube（samplerCube: RGB=S, A=T）
		.AddUnifromTexture(2)					// 背景 cube（samplerCube: L_bg）
		.AddStorageImage2DArray(3);				// 输出 finalSkyCube（rgba16f，只写 level 0）

	if (config.Validate())
		_compositeShader.Create(config);
}

IBLPreCalculatePass::SourceSelection IBLPreCalculatePass::SelectSources(const RenderState& state) const
{
	SourceSelection sel;

	// 背景：大气优先，其次天空盒。两者 ShouldExecute 互斥，这里按优先级写死，保证切场景时决策稳定。
	const auto& atmo = state.skyAtmosphereParams;
	if (atmo.hasSkyAtmospherePreData && atmo.skyCube && !atmo.skyCube->IsEmpty())
	{
		sel.background = BackgroundKind::Atmosphere;
		sel.backgroundCube = atmo.skyCube;
		sel.backgroundRevision = atmo.skyCubeRevision;
	}
	else
	{
		const auto& skybox = state.skyboxParams;
		if (skybox.hasSkyBoxPreData && skybox.skyCube && !skybox.skyCube->IsEmpty())
		{
			sel.background = BackgroundKind::SkyBox;
			sel.backgroundCube = skybox.skyCube;
			sel.backgroundRevision = skybox.skyCubeRevision;
		}
	}

	// 云：独立可选源，只有云参数变化才更新自己的 cube；背景变化不影响云 cube。
	const auto& cloud = state.cloudCubeParams;
	if (cloud.hasCloudPreData && cloud.skyCube && !cloud.skyCube->IsEmpty())
	{
		sel.cloudCube = cloud.skyCube;
		sel.cloudRevision = cloud.skyCubeRevision;
	}

	return sel;
}

void IBLPreCalculatePass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	const SourceSelection sel = SelectSources(state);
	const bool hasCloud = sel.cloudCube && !sel.cloudCube->IsEmpty();
	const bool hasBackground = sel.backgroundCube && !sel.backgroundCube->IsEmpty();

	// 一个源都没有：IBL 为空。消费者用 IBLReady() 判断，走无环境光的分支。
	if (!hasCloud && !hasBackground)
	{
		state.iblParams = {};
		_lastSelection = {};
		return;
	}

	// 有云时无论有没有背景都要合成（有背景则 S + T*L_bg，无背景则 L_bg = 0）；
	// 只有背景、没有云时直接用背景 cube，省掉一次合成 dispatch。
	const bool needComposite = hasCloud;
	const bool needUpdate = sel != _lastSelection;
	const std::shared_ptr<TextureCube> iblSource = needComposite ? _finalSkyCube : sel.backgroundCube;

	if (needUpdate)
	{
		auto cmd = cmdCtx.GetCmd();

		if (needComposite)
		{
			CompositeParams composite{};
			composite.BackgroundEnable = hasBackground ? 1u : 0u;
			auto compositeUBO = std::make_shared<UniformBlock>(sizeof(composite));
			compositeUBO->WriteDataAsync(cmd, &composite, sizeof(composite));

			ComputeBindingRecord compositeBinding;
			compositeBinding.SetUniformBlock(compositeUBO, 0);
			compositeBinding.SetUniformTextureCube(sel.cloudCube, vk::ImageAspectFlagBits::eColor, 1);
			compositeBinding.SetUniformTextureCube(hasBackground ? sel.backgroundCube : _blackCube, vk::ImageAspectFlagBits::eColor, 2);
			compositeBinding.SetStorageImage2DArrayLevel(_finalSkyCube, vk::ImageAspectFlagBits::eColor, 0, 1, 3);
			_compositeShader.Bind(cmd, compositeBinding);
			cmd->dispatch(
				(_finalSkyCube->GetWidth() + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X,
				(_finalSkyCube->GetHeight() + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y,
				6);

			_finalSkyCube->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
			_finalSkyCube->GenerateTextureMipMapsAsync(cmd);
			_finalSkyCube->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
		}

		// IBL 三件套。BRDF LUT 不依赖天空图，只算一次。
		{
			IBLImageBuilder::DiffuseGenerateParams diffuseParams{};
			_iblBuilder.CalculateCubeDiffuse(cmd, _diffuse, iblSource, diffuseParams);
		}

		{
			IBLImageBuilder::PrefilterGenerateParams prefilterParams{};
			_iblBuilder.CalculateCubePrefilter(cmd, _prefilter, iblSource, prefilterParams);
		}

		if (!_brdfLUT)
		{
			_brdfLUT = std::make_shared<Texture2D>(512, 512, vk::Format::eR16G16B16A16Sfloat, TextureConfig::GetDefaultSkyCubeConfig());
			IBLImageBuilder::BRDFLUTGenerateParams lutParams{};
			_iblBuilder.CalculateBRDFLUT(cmd, _brdfLUT, lutParams);
		}

		if (cmd->IsRecording())
			cmd->SubmitToQueue();

		_lastSelection = sel;
	}

	// 无论本帧是否重算，都把缓存的 IBL 与最终天空暴露给同帧消费者
	state.iblParams.diffuse = _diffuse;
	state.iblParams.prefilter = _prefilter;
	state.iblParams.brdfLUT = _brdfLUT;
	state.iblParams.finalSkyCube = iblSource;
}

#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/LightingPass.h"
#include "VulkanRenderEngine/General/RenderHelp.h"
#include "VulkanRenderEngine/Base/Light.h"
#include "VulkanRenderEngine/Base/AtlasMap.h"

struct IBLParams
{
	uint32_t IBLEnable = 0;
};

LightingPass::LightingPass(const std::string& computeShaderPath)
{
	ComputePipelineConfig config;
	config.computePath = computeShaderPath;
	config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
	config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);

	config
		.AddCameraUnifromDataBinding()
		.AddLightDataBinding()
		.AddUnifromBuffer(0)
		.AddStorageImage(1)
		.AddUnifromTexture(2)
		.AddUnifromTexture(3)
		.AddUnifromTexture(4)
		.AddUnifromTexture(5)
		.AddUnifromTexture(6)
		.AddUnifromTexture(7)
		.AddUnifromTexture(8)
		.AddUnifromTexture(9)
		.AddUnifromTexture(10)
		.AddUnifromTexture(11)
		.AddUnifromTexture(12);

	_shader.Create(config);
}

LightingPass::~LightingPass() {}

void LightingPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{}

void LightingPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	auto gPosition = ctx.GetInput(0);
	auto gNormal = ctx.GetInput(1);
	auto gAlbedoOpacity = ctx.GetInput(2);
	auto gMetallicRoughness = ctx.GetInput(3);
	auto atlasShadowMap = ctx.GetInput(4);
	auto ssao = ctx.GetInput(5);
	auto gEmission = ctx.GetInput(6);
	auto gDepthStencilMap = ctx.GetInput(7);

	auto target = ctx.GetExternal(0);


	bool IBLEnable = false;
	std::shared_ptr<TextureCube> IBLDiffuse;
	std::shared_ptr<TextureCube> IBLPrefilter;
	std::shared_ptr<Texture2D> IBLBrdfLUT;

	// 如果没有开启间接光照相关的Pass，则在这里直接进行sky反射捕获
	if (!state.option.flags.rayTraceReflectOn 
		&& !state.option.flags.rayTraceGIOn 
		&& !state.option.flags.ssrOn 
		&& !state.option.flags.ssgiOn)
	{
		if (state.skyAtmosphereParams.hasSkyAtmospherePreData)
		{
			if (
				state.skyAtmosphereParams.skyCubeDiffuse
				&& !state.skyAtmosphereParams.skyCubeDiffuse->IsEmpty()
				&& state.skyAtmosphereParams.skyCubePrefilter
				&& !state.skyAtmosphereParams.skyCubePrefilter->IsEmpty()
				&& state.skyAtmosphereParams.brdfLUT
				&& !state.skyAtmosphereParams.brdfLUT->IsEmpty()
				)
			{
				IBLEnable = true;
				IBLDiffuse = state.skyAtmosphereParams.skyCubeDiffuse;
				IBLPrefilter = state.skyAtmosphereParams.skyCubePrefilter;
				IBLBrdfLUT = state.skyAtmosphereParams.brdfLUT;
			}
		}
		else if (state.skyboxParams.hasSkyBoxPreData)
		{
			if (
				state.skyboxParams.skyCubeDiffuse
				&& !state.skyboxParams.skyCubeDiffuse->IsEmpty()
				&& state.skyboxParams.skyCubePrefilter
				&& !state.skyboxParams.skyCubePrefilter->IsEmpty()
				&& state.skyboxParams.brdfLUT
				&& !state.skyboxParams.brdfLUT->IsEmpty()
				)
			{
				IBLEnable = true;
				IBLDiffuse = state.skyboxParams.skyCubeDiffuse;
				IBLPrefilter = state.skyboxParams.skyCubePrefilter;
				IBLBrdfLUT = state.skyboxParams.brdfLUT;
			}
		}
	}

	IBLParams params{
		.IBLEnable = uint32_t(IBLEnable)
	};

	auto cmd = cmdCtx.GetCmd();

	auto paramsUBO = std::make_shared<UniformBlock>(sizeof(params));
	paramsUBO->WriteDataAsync(cmd, &params, sizeof(params));
	paramsUBO->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::UniformRead);


	ComputeBindingRecord binding;

	binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	binding.SetUniformBlock(paramsUBO, 0);
	binding.SetStorageImage(target, vk::ImageAspectFlagBits::eColor, 1);
	binding.SetUniformTexture(gPosition, vk::ImageAspectFlagBits::eColor, 2);
	binding.SetUniformTexture(gNormal, vk::ImageAspectFlagBits::eColor, 3);
	binding.SetUniformTexture(gAlbedoOpacity, vk::ImageAspectFlagBits::eColor, 4);
	binding.SetUniformTexture(gMetallicRoughness, vk::ImageAspectFlagBits::eColor, 5);
	binding.SetUniformTexture(gEmission, vk::ImageAspectFlagBits::eColor, 6);
	binding.SetUniformTexture(ssao, vk::ImageAspectFlagBits::eColor, 7);
	binding.SetUniformTexture(atlasShadowMap, vk::ImageAspectFlagBits::eDepth, 8);
	binding.SetUniformTexture(gDepthStencilMap, vk::ImageAspectFlagBits::eDepth, 9);

	if (params.IBLEnable)
	{
		binding.SetUniformTextureCube(IBLDiffuse, vk::ImageAspectFlagBits::eColor, 10);
		binding.SetUniformTextureCube(IBLPrefilter, vk::ImageAspectFlagBits::eColor, 11);
		binding.SetUniformTexture(IBLBrdfLUT, vk::ImageAspectFlagBits::eColor, 12);
	}

	binding.SetLightStorageData(
		state.lights.ssbo_dirLightMeta,
		state.lights.ssbo_dirLightCascade,
		state.lights.ssbo_pointLightMeta,
		state.lights.ssbo_spotLightMeta
	);

	_shader.Bind(cmd, binding);
	cmd->dispatch((state.framebuffer.width + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (state.framebuffer.height + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

	cmd->SubmitToQueue();
}

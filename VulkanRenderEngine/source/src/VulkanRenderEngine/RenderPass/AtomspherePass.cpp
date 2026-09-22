#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/AtomspherePass.h"

constexpr uint32_t work_size_x = 16;
constexpr uint32_t work_size_y = 16;

struct alignas(16) AtomsphereParams
{
	alignas(16) glm::vec3 DirLightColor;
	alignas(16) glm::vec3 DirLightDir;
	float PlanetRadius;
	float AtmosphereHeight;
	float RayleighScatteringScalarHeight;
	float MieScatteringScalarHeight;
	float MieAnisotropy;
	float OzoneLevelCenterHeight;
	float OzoneLevelWidth;
	uint32_t ScatterPathSampleCount;		// 沿着路径上的散射采样数
	uint32_t TransmittanceSampleCount;		// 对两点之间透射率计算的采样数
};


AtomspherePass::AtomspherePass(const std::string& computeShaderPath)
{
	ComputePipelineConfig config;
	config.AddDefineMacro("work_size_x", work_size_x);
	config.AddDefineMacro("work_size_y", work_size_y);
	config.computePath = computeShaderPath;

	config
		.AddCameraUnifromDataBinding()
		.AddUnifromBuffer(0)
		.AddStorageImage(1)
		.AddUnifromTexture(2)
		.AddUnifromTexture(3);

	if (config.Validate())
		_shader.Create(config);

}

bool AtomspherePass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return state.option.flags.atomsphereOn && !state.lights.dirLightInfos.empty();
}

void AtomspherePass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!ShouldExecute(registry, state))
		return;

	auto& binding = *registry.Get<ComputeBindingRecord>("binding");
	auto paramsUBO = std::make_shared<UniformBlock>(sizeof(AtomsphereParams));

	auto& dirLight = state.lights.dirLightInfos[0]->light;

	AtomsphereParams params
	{
		.DirLightColor = dirLight->getColor() * dirLight->getIntensity(),
		.DirLightDir = dirLight->getDirection(),
		.PlanetRadius = state.option.atomsphereParams.PlanetRadius,
		.AtmosphereHeight = state.option.atomsphereParams.AtmosphereHeight,
		.RayleighScatteringScalarHeight = state.option.atomsphereParams.RayleighScatteringScalarHeight,
		.MieScatteringScalarHeight = state.option.atomsphereParams.MieScatteringScalarHeight,
		.MieAnisotropy = state.option.atomsphereParams.MieAnisotropy,
		.OzoneLevelCenterHeight = state.option.atomsphereParams.OzoneLevelCenterHeight,
		.OzoneLevelWidth = state.option.atomsphereParams.OzoneLevelWidth,
		.ScatterPathSampleCount = state.option.atomsphereParams.ScatterPathSampleCount,
		.TransmittanceSampleCount = state.option.atomsphereParams.TransmittanceSampleCount
	};

	paramsUBO->WriteData(&params, sizeof(AtomsphereParams));
	binding.SetUniformBlock(paramsUBO, 0);
}

void AtomspherePass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	auto sceneColorBuffer = ctx.GetExternal(0);
	auto sceneDepthBuffer = ctx.GetExternal(1);

	auto tempColor = ctx.GetTemp(0);

	if (!sceneColorBuffer
		|| !sceneDepthBuffer
		|| !tempColor
		|| sceneColorBuffer->IsEmpty()
		|| sceneDepthBuffer->IsEmpty()
		|| tempColor->IsEmpty()
		)
		return;

	auto cmd = cmdCtx.GetCmd();

	Texture2D::CopyTextureAsync(cmd, sceneColorBuffer, tempColor);

	uint32_t width = sceneColorBuffer->GetWidth();
	uint32_t height = sceneColorBuffer->GetHeight();


	auto& binding = *registry.Get<ComputeBindingRecord>("binding");

	binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	binding.SetStorageImage(sceneColorBuffer, vk::ImageAspectFlagBits::eColor, 1);
	binding.SetUniformTexture(tempColor, vk::ImageAspectFlagBits::eColor, 2);
	binding.SetUniformTexture(sceneDepthBuffer, vk::ImageAspectFlagBits::eDepth, 3);

	_shader.Bind(cmd, binding);
	cmd->dispatch((width + work_size_x - 1) / work_size_x, (height + work_size_y - 1) / work_size_y, 1);

	cmd->SubmitToQueue();
}
#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/LightingPass.h"
#include "VulkanRenderEngine/General/RenderHelper.h"
#include "VulkanRenderEngine/Base/Light.h"
#include "VulkanRenderEngine/Base/AtlasMap.h"
#include "VulkanRenderEngine/General/IndirectLightingPolicy.h"

namespace
{
	// 与 lightingpass.comp 的 IBLParams 对应（std140 bool = 4 字节）
	struct IBLParams
	{
		uint32_t IBLDiffuseEnable = 0;
		uint32_t IBLSpecularEnable = 0;
	};
}

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


	// IBL 按漫反射 / 镜面两个维度分别补：哪个维度已经有间接光 Pass 负责，这里就不加（分工见 IndirectLightingPolicy.h）
	//   SSR / RTR 开启   → 不补镜面
	//   DDGI 开启        → 不补漫反射（体积没覆盖到的部分由 DDGIApply 回退到 IBL 漫反射）
	//   SSGI / RTGI 开启 → 路径追踪同时含漫反射与镜面，两项都不补
	const IndirectLightingPolicy policy = GetIndirectLightingPolicy(state);
	const IBLResources ibl = SelectIBLResources(state);
	const bool iblReady = (policy.lightingIBLDiffuse || policy.lightingIBLSpecular) && ibl.IBLReady();

	IBLParams params;
	params.IBLDiffuseEnable = iblReady && policy.lightingIBLDiffuse;
	params.IBLSpecularEnable = iblReady && policy.lightingIBLSpecular;

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

	if (iblReady)
	{
		binding.SetUniformTextureCube(ibl.diffuse, vk::ImageAspectFlagBits::eColor, 10);
		binding.SetUniformTextureCube(ibl.prefilter, vk::ImageAspectFlagBits::eColor, 11);
		binding.SetUniformTexture(ibl.brdfLUT, vk::ImageAspectFlagBits::eColor, 12);
	}

	binding.SetLightStorageData(
		state.lights.ssbo_dirLightMeta,
		state.lights.ssbo_dirLightCascade,
		state.lights.ssbo_pointLightMeta,
		state.lights.ssbo_spotLightMeta,
		state.lights.ssbo_dirLightCascadeDistances
	);

	_shader.Bind(cmd, binding);
	cmd->dispatch((state.framebuffer.width + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (state.framebuffer.height + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

	cmd->SubmitToQueue();
}

#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/SkyIBLDiffusePreCalculatePass.h"

SkyIBLDiffusePreCalculatePass::SkyIBLDiffusePreCalculatePass()
{
	// 与 IBLPreCalculatePass 的 diffuse cube 同规格：辐照度很平滑，32² 每面足够，单级 mip
	constexpr uint32_t diffuseCubeSize = 32;
	_skyDiffuse = std::make_shared<TextureCube>(diffuseCubeSize, vk::Format::eR16G16B16A16Sfloat, TextureConfig::GetDefaultSkyCubeConfig());

	// 清零为黑：没有任何背景时它就是云的环境光（= 0），同时保证内容与布局始终有效、可直接绑定
	auto cmd = VKCONTEXT->GetCommandBuffer();
	cmd->Begin();

	vk::ImageLayout layout = _skyDiffuse->GetLayout(0);
	_skyDiffuse->TransitionLayout(cmd, &layout, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	vk::ImageSubresourceRange range;
	range.setAspectMask(vk::ImageAspectFlagBits::eColor)
		.setBaseMipLevel(0)
		.setLevelCount(1)
		.setBaseArrayLayer(0)
		.setLayerCount(6);
	cmd->clearColorImage(_skyDiffuse->GetImage(), layout, vk::ClearColorValue{ std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f } }, range);

	_skyDiffuse->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);

	cmd->End();
	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
}

void SkyIBLDiffusePreCalculatePass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	// 背景选择与 IBLPreCalculatePass 一致：大气优先，其次天空盒
	std::shared_ptr<TextureCube> source;
	uint64_t sourceRevision = 0;

	const auto& atmo = state.skyAtmosphereParams;
	if (atmo.hasSkyAtmospherePreData && atmo.skyCube && !atmo.skyCube->IsEmpty())
	{
		source = atmo.skyCube;
		sourceRevision = atmo.skyCubeRevision;
	}
	else
	{
		const auto& skybox = state.skyboxParams;
		if (skybox.hasSkyBoxPreData && skybox.skyCube && !skybox.skyCube->IsEmpty())
		{
			source = skybox.skyCube;
			sourceRevision = skybox.skyCubeRevision;
		}
	}

	if (source && (!_generated || source != _lastSource || sourceRevision != _lastSourceRevision))
	{
		auto cmd = cmdCtx.GetCmd();

		IBLImageBuilder::DiffuseGenerateParams params;
		_iblBuilder.CalculateCubeDiffuse(cmd, _skyDiffuse, source, params);
		_skyDiffuse->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);

		if (cmd->IsRecording())
			cmd->SubmitToQueue();

		_lastSource = source;
		_lastSourceRevision = sourceRevision;
		_generated = true;
		++_skyDiffuseRevision;
	}

	// 永远暴露（无背景时是黑 cube）：消费端（云 / 体积）可以直接绑定，不需要空指针分支
	state.iblParams.skyDiffuse = _skyDiffuse;
	state.iblParams.skyDiffuseRevision = _skyDiffuseRevision;
}

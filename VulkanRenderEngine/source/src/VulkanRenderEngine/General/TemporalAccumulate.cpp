#include "vkstdafx.h"
#include "VulkanRenderEngine/General/TemporalAccumulate.h"


TemporalAccumulate::TemporalAccumulate(const std::string& temporalAccumulateShaderPath)
{

	ComputePipelineConfig config;
	config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
	config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
	config.computePath = temporalAccumulateShaderPath;

	config
		.AddCameraUnifromDataBinding()
		.AddUnifromBuffer(0)
		.AddStorageImage(1)
		.AddStorageImage(2)
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

	if (config.Validate())
		_pipeline.Create(config);

}

void TemporalAccumulate::Execute(
	const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
	const std::shared_ptr<Texture2D>& originImage,
	const std::shared_ptr<Texture2D>& positionMap,
	const std::shared_ptr<Texture2D>& normalMap,
	const std::shared_ptr<Texture2D>& depthMap,
	const std::shared_ptr<Texture2D>& motionVectorMap,
	const std::shared_ptr<Texture2D>& prevPositionMap,
	const std::shared_ptr<Texture2D>& prevNormalMap,
	const std::shared_ptr<Texture2D>& prevDepthMap,
	const std::shared_ptr<Texture2D>& outputColorImage,
	const std::shared_ptr<Texture2D>& outputMomentImage,
	const std::shared_ptr<Texture2D>& historyColorImage,
	const std::shared_ptr<Texture2D>& historyMomentImage,
	const Params& params,
	std::shared_ptr<UniformBlock>& curCameraUBO,
	std::shared_ptr<UniformBlock>& prevCameraUBO
)
{
	auto size = outputColorImage->GetSize();

	auto paramsUBO = std::make_shared<UniformBlock>(sizeof(params));
	paramsUBO->WriteDataAsync(cmd, &params, sizeof(params));

	outputColorImage->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	vk::ClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	vk::ClearColorValue clearMoments = { 0.0f, 0.0f, 0.0f, 0.0f };
	vk::ImageSubresourceRange range;
	range.setAspectMask(vk::ImageAspectFlagBits::eColor)
		.setBaseArrayLayer(0)
		.setLayerCount(1)
		.setBaseMipLevel(0)
		.setLevelCount(1);
	cmd->clearColorImage(outputColorImage->GetImage(), vk::ImageLayout::eGeneral, clearColor, range);
	cmd->clearColorImage(outputMomentImage->GetImage(), vk::ImageLayout::eGeneral, clearMoments, range);

	ComputeBindingRecord binding;
	binding.SetCameraUnifromData(curCameraUBO, prevCameraUBO);
	binding.SetUniformBlock(paramsUBO, 0);
	binding.SetStorageImage(outputColorImage, vk::ImageAspectFlagBits::eColor, 1);
	binding.SetStorageImage(outputMomentImage, vk::ImageAspectFlagBits::eColor, 2);
	binding.SetUniformTexture(originImage, vk::ImageAspectFlagBits::eColor, 3);
	binding.SetUniformTexture(historyColorImage, vk::ImageAspectFlagBits::eColor, 4);
	binding.SetUniformTexture(historyMomentImage, vk::ImageAspectFlagBits::eColor, 5);
	binding.SetUniformTexture(positionMap, vk::ImageAspectFlagBits::eColor, 6);
	binding.SetUniformTexture(normalMap, vk::ImageAspectFlagBits::eColor, 7);
	binding.SetUniformTexture(depthMap, vk::ImageAspectFlagBits::eDepth, 8);
	binding.SetUniformTexture(motionVectorMap, vk::ImageAspectFlagBits::eColor, 9);
	binding.SetUniformTexture(prevPositionMap, vk::ImageAspectFlagBits::eColor, 10);
	binding.SetUniformTexture(prevNormalMap, vk::ImageAspectFlagBits::eColor, 11);
	binding.SetUniformTexture(prevDepthMap, vk::ImageAspectFlagBits::eDepth, 12);

	_pipeline.Bind(cmd, binding);
	cmd->dispatch((size.x + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (size.y + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

}

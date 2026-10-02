#include "vkstdafx.h"
#include "VulkanRenderEngine/General/AtrousBilateralFilter.h"

struct PushConstant {
	int curPassIndex;
};

AtrousBilateralFilter::AtrousBilateralFilter(const std::string& atrousComputeShaderPath)
{
	ComputePipelineConfig config;
	config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
	config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
	config.computePath = atrousComputeShaderPath;

	config
		.AddCameraUnifromDataBinding()
		.AddUnifromBuffer(0)
		.AddStorageImage(1)
		.AddUnifromTexture(2)
		.AddUnifromTexture(3)
		.AddUnifromTexture(4)
		.AddUnifromTexture(5)
		.AddPushConstant(sizeof(PushConstant));

	if (config.Validate())
		_pipeline.Create(config);
}

void AtrousBilateralFilter::Execute(
	const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
	const std::shared_ptr<Texture2D>& originImage,
	const std::shared_ptr<Texture2D>& normalMap,
	const std::shared_ptr<Texture2D>& depthMap,
	const std::shared_ptr<Texture2D>& momentsMap,
	const std::shared_ptr<Texture2D>& outputImage,
	const std::shared_ptr<Texture2D>& tempImage,
	const Params& params,
	std::shared_ptr<UniformBlock>& curCameraUBO,
	std::shared_ptr<UniformBlock>& prevCameraUBO,
	uint32_t filterCount
)
{
	if (filterCount <= 0)
	{
		Texture2D::CopyTextureAsync(cmd, originImage, outputImage);
		return;
	}

	outputImage->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
	tempImage->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	vk::ClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
	vk::ImageSubresourceRange range;
	range.setAspectMask(vk::ImageAspectFlagBits::eColor)
		.setBaseArrayLayer(0)
		.setLayerCount(1)
		.setBaseMipLevel(0)
		.setLevelCount(1);
	cmd->clearColorImage(outputImage->GetImage(), vk::ImageLayout::eGeneral, clearColor, range);
	cmd->clearColorImage(tempImage->GetImage(), vk::ImageLayout::eGeneral, clearColor, range);

	auto imageSize = outputImage->GetSize();

	auto paramsUBO = std::make_shared<UniformBlock>(sizeof(params));
	paramsUBO->WriteDataAsync(cmd, &params, sizeof(params));

	ComputeBindingRecord binding;
	binding.SetUniformBlock(paramsUBO, 0);

	binding.SetCameraUnifromData(curCameraUBO, prevCameraUBO);
	binding.SetUniformTexture(normalMap, vk::ImageAspectFlagBits::eColor, 3);
	binding.SetUniformTexture(depthMap, vk::ImageAspectFlagBits::eDepth, 4);
	binding.SetUniformTexture(momentsMap, vk::ImageAspectFlagBits::eColor, 5);

	PushConstant pc_params{ .curPassIndex = 0 };

	bool first_iteration = true;
	std::shared_ptr<Texture2D> DrawImage = filterCount % 2 == 0 ? tempImage : outputImage;
	std::shared_ptr<Texture2D> SampleImage = originImage;
	for (uint32_t i = 0; i < filterCount; i++)
	{

		DrawImage->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
		SampleImage->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);

		binding.SetStorageImage(DrawImage, vk::ImageAspectFlagBits::eColor, 1);
		binding.SetUniformTexture(SampleImage, vk::ImageAspectFlagBits::eColor, 2);
		_pipeline.SetPushConstants(cmd, &pc_params, sizeof(pc_params));

		_pipeline.Bind(cmd, binding);
		cmd->dispatch((imageSize.x + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (imageSize.y + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

		pc_params.curPassIndex++;
		if (first_iteration)
		{
			first_iteration = false;
			DrawImage = filterCount % 2 == 0 ? outputImage : tempImage;
			SampleImage = filterCount % 2 == 0 ? tempImage : outputImage;
		}
		else
			std::swap(DrawImage, SampleImage);
	}

}

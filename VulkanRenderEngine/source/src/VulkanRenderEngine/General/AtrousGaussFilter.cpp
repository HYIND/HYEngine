#include "vkstdafx.h"
#include "VulkanRenderEngine/General/AtrousGaussFilter.h"


AtrousGaussFilter::AtrousGaussFilter(const std::string& computeShaderPath)
{
	ComputePipelineConfig config;
	config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
	config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
	config.computePath = computeShaderPath;

	config
		.AddUnifromBuffer(0)
		.AddStorageImage(1)
		.AddUnifromTexture(2);

	if (config.Validate())
		_pipeline.Create(config);
}

void AtrousGaussFilter::Execute(
	const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
	const std::shared_ptr<Texture2D>& originImage,
	const std::shared_ptr<Texture2D>& outputImage,
	Params params
)
{
	if (params.kernelRadius == 0 || params.kernelStep == 0)
	{
		Texture2D::CopyTextureAsync(cmd, originImage, outputImage);
		return;
	}

	params.sigma = std::max(params.sigma, 0.02f);

	outputImage->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	auto imageSize = outputImage->GetSize();

	auto paramsUBO = std::make_shared<UniformBlock>(sizeof(params));
	paramsUBO->WriteDataAsync(cmd, &params, sizeof(params));

	ComputeBindingRecord binding;
	binding.SetUniformBlock(paramsUBO, 0);
	binding.SetStorageImage(outputImage, vk::ImageAspectFlagBits::eColor, 1);
	binding.SetUniformTexture(originImage, vk::ImageAspectFlagBits::eColor, 2);

	_pipeline.Bind(cmd, binding);
	cmd->dispatch((imageSize.x + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (imageSize.y + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);
}

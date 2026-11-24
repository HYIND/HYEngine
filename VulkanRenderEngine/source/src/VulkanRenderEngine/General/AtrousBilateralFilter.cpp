#include "vkstdafx.h"
#include "VulkanRenderEngine/General/AtrousBilateralFilter.h"

struct PushConstant {
	int curPassIndex;
};

AtrousBilateralFilter::AtrousBilateralFilter(const std::string& atrousComputeShaderPath, const std::string& varianceShaderPath)
{
	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
		config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
		config.computePath = varianceShaderPath;

		config
			.AddCameraUnifromDataBinding()
			.AddUnifromBuffer(0)		// Params（与 À-trous 同一份）
			.AddStorageImage(1)			// 方差输出 R32F
			.AddUnifromTexture(3)		// 法线
			.AddUnifromTexture(4)		// 深度
			.AddUnifromTexture(5);		// 时域矩

		if (config.Validate())
			_variancePipeline.Create(config);
	}

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
		.AddUnifromTexture(6)		// 上一轮方差
		.AddStorageImage(7)			// 本轮方差
		.AddUnifromTexture(8)		// metallic / roughness（镜面信号）
		.AddPushConstant(sizeof(PushConstant));

	if (config.Validate())
		_pipeline.Create(config);
}

void AtrousBilateralFilter::EnsureVarianceTextures(uint32_t width, uint32_t height)
{
	for (auto& tex : _variance)
	{
		if (!tex || tex->IsEmpty() || tex->GetWidth() != width || tex->GetHeight() != height)
			tex = std::make_shared<Texture2D>(width, height, vk::Format::eR32Sfloat);
	}
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
	uint32_t filterCount,
	const std::shared_ptr<Texture2D>& metallicRoughnessMap
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

	// 镜面模式缺粗糙度图时退回普通滤波
	Params shaderParams = params;
	if (!metallicRoughnessMap || metallicRoughnessMap->IsEmpty())
		shaderParams.specularMode = 0;

	auto paramsUBO = std::make_shared<UniformBlock>(sizeof(shaderParams));
	paramsUBO->WriteDataAsync(cmd, &shaderParams, sizeof(shaderParams));

	const uint32_t groupX = (imageSize.x + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X;
	const uint32_t groupY = (imageSize.y + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y;

	// SVGF 方差估计：写 _variance[0]（每个像素都写，天空为 -1，不需要清）
	EnsureVarianceTextures(imageSize.x, imageSize.y);
	{
		_variance[0]->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

		ComputeBindingRecord varianceBinding;
		varianceBinding.SetCameraUnifromData(curCameraUBO, prevCameraUBO);
		varianceBinding.SetUniformBlock(paramsUBO, 0);
		varianceBinding.SetStorageImage(_variance[0], vk::ImageAspectFlagBits::eColor, 1);
		varianceBinding.SetUniformTexture(normalMap, vk::ImageAspectFlagBits::eColor, 3);
		varianceBinding.SetUniformTexture(depthMap, vk::ImageAspectFlagBits::eDepth, 4);
		varianceBinding.SetUniformTexture(momentsMap, vk::ImageAspectFlagBits::eColor, 5);

		_variancePipeline.Bind(cmd, varianceBinding);
		cmd->dispatch(groupX, groupY, 1);
	}

	ComputeBindingRecord binding;
	binding.SetUniformBlock(paramsUBO, 0);

	binding.SetCameraUnifromData(curCameraUBO, prevCameraUBO);
	binding.SetUniformTexture(normalMap, vk::ImageAspectFlagBits::eColor, 3);
	binding.SetUniformTexture(depthMap, vk::ImageAspectFlagBits::eDepth, 4);
	binding.SetUniformTexture(momentsMap, vk::ImageAspectFlagBits::eColor, 5);
	binding.SetUniformTexture(shaderParams.specularMode != 0 ? metallicRoughnessMap : normalMap, vk::ImageAspectFlagBits::eColor, 8);

	PushConstant pc_params{ .curPassIndex = 0 };

	bool first_iteration = true;
	std::shared_ptr<Texture2D> DrawImage = filterCount % 2 == 0 ? tempImage : outputImage;
	std::shared_ptr<Texture2D> SampleImage = originImage;
	for (uint32_t i = 0; i < filterCount; i++)
	{

		// 方差乒乓：第 i 轮读 _variance[i % 2]，写 _variance[(i + 1) % 2]
		auto& varianceIn = _variance[i % 2];
		auto& varianceOut = _variance[(i + 1) % 2];

		DrawImage->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
		SampleImage->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
		varianceOut->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
		varianceIn->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);

		binding.SetStorageImage(DrawImage, vk::ImageAspectFlagBits::eColor, 1);
		binding.SetUniformTexture(SampleImage, vk::ImageAspectFlagBits::eColor, 2);
		binding.SetUniformTexture(varianceIn, vk::ImageAspectFlagBits::eColor, 6);
		binding.SetStorageImage(varianceOut, vk::ImageAspectFlagBits::eColor, 7);
		_pipeline.SetPushConstants(cmd, &pc_params, sizeof(pc_params));

		_pipeline.Bind(cmd, binding);
		cmd->dispatch(groupX, groupY, 1);

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

#include "vkstdafx.h"
#include "VulkanRenderEngine/General/TAAFilter.h"

TAAFilter::TAAFilter(const std::string& shaderPath)
{
	ComputePipelineConfig config;
	config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
	config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
	config.computePath = shaderPath;

	config
		.AddCameraUnifromDataBinding()
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
		.AddUnifromTexture(10);

	if (config.Validate())
		_pipeline.Create(config);
}

glm::vec2 TAAFilter::GetJitterPixels(uint32_t frameIndex)
{
	auto halton = [](uint32_t index, uint32_t base)
		{
			float value = 0.0f;
			float fraction = 1.0f;
			while (index > 0)
			{
				fraction /= static_cast<float>(base);
				value += fraction * static_cast<float>(index % base);
				index /= base;
			}
			return value;
		};

	const uint32_t sequenceIndex = frameIndex % 8 + 1;
	return glm::vec2(halton(sequenceIndex, 2), halton(sequenceIndex, 3)) - glm::vec2(0.5f);
}

void TAAFilter::Execute(
	const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
	const std::shared_ptr<Texture2D>& currentColor,
	const std::shared_ptr<Texture2D>& historyColor,
	const std::shared_ptr<Texture2D>& currentPosition,
	const std::shared_ptr<Texture2D>& currentNormal,
	const std::shared_ptr<Texture2D>& currentDepth,
	const std::shared_ptr<Texture2D>& motionVector,
	const std::shared_ptr<Texture2D>& previousPosition,
	const std::shared_ptr<Texture2D>& previousNormal,
	const std::shared_ptr<Texture2D>& previousDepth,
	const std::shared_ptr<Texture2D>& outputColor,
	const Params& params,
	std::shared_ptr<UniformBlock>& currentCameraUBO,
	std::shared_ptr<UniformBlock>& previousCameraUBO)
{
	if (!cmd || !currentColor || !historyColor || !currentPosition || !currentDepth || !currentNormal
		|| !motionVector || !previousPosition || !previousDepth || !previousNormal || !outputColor
		|| !currentCameraUBO || !previousCameraUBO
		|| currentColor->IsEmpty() || historyColor->IsEmpty()
		|| currentPosition->IsEmpty() || currentDepth->IsEmpty() || currentNormal->IsEmpty()
		|| motionVector->IsEmpty() || previousPosition->IsEmpty() || previousDepth->IsEmpty() || previousNormal->IsEmpty()
		|| outputColor->IsEmpty())
		return;

	auto size = outputColor->GetSize();

	auto paramsUBO = std::make_shared<UniformBlock>(sizeof(params));
	paramsUBO->WriteDataAsync(cmd, &params, sizeof(params));

	outputColor->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	ComputeBindingRecord binding;
	binding.SetCameraUnifromData(currentCameraUBO, previousCameraUBO);
	binding.SetUniformBlock(paramsUBO, 0);
	binding.SetStorageImage(outputColor, vk::ImageAspectFlagBits::eColor, 1);
	binding.SetUniformTexture(currentColor, vk::ImageAspectFlagBits::eColor, 2);
	binding.SetUniformTexture(historyColor, vk::ImageAspectFlagBits::eColor, 3);
	binding.SetUniformTexture(currentPosition, vk::ImageAspectFlagBits::eColor, 4);
	binding.SetUniformTexture(currentNormal, vk::ImageAspectFlagBits::eColor, 5);
	binding.SetUniformTexture(currentDepth, vk::ImageAspectFlagBits::eDepth, 6);
	binding.SetUniformTexture(motionVector, vk::ImageAspectFlagBits::eColor, 7);
	binding.SetUniformTexture(previousPosition, vk::ImageAspectFlagBits::eColor, 8);
	binding.SetUniformTexture(previousNormal, vk::ImageAspectFlagBits::eColor, 9);
	binding.SetUniformTexture(previousDepth, vk::ImageAspectFlagBits::eDepth, 10);

	_pipeline.Bind(cmd, binding);
	cmd->dispatch((size.x + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X,
		(size.y + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);
}

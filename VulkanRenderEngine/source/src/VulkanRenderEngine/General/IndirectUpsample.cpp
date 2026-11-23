#include "vkstdafx.h"
#include "VulkanRenderEngine/General/IndirectUpsample.h"

namespace
{
	struct UpsamplePushConstant {
		uint32_t mode;
	};
}

IndirectUpsample::IndirectUpsample(const std::string& shaderPath)
{
	ComputePipelineConfig config;
	config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
	config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
	config.computePath = shaderPath;

	config
		.AddCameraUnifromDataBinding()
		.AddStorageImage(1)			// 全分辨率输出
		.AddUnifromTexture(2)		// 低分辨率降噪结果
		.AddUnifromTexture(3)		// 法线
		.AddUnifromTexture(4)		// 深度
		.AddUnifromTexture(5)		// albedo
		.AddUnifromTexture(6)		// metallic / roughness
		.AddUnifromTexture(7)		// 位置
		.AddPushConstant(sizeof(UpsamplePushConstant));

	if (config.Validate())
		_pipeline.Create(config);
}

void IndirectUpsample::Execute(
	const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
	const std::shared_ptr<Texture2D>& lowResImage,
	const std::shared_ptr<Texture2D>& positionMap,
	const std::shared_ptr<Texture2D>& normalMap,
	const std::shared_ptr<Texture2D>& depthMap,
	const std::shared_ptr<Texture2D>& albedoMap,
	const std::shared_ptr<Texture2D>& metallicRoughnessMap,
	const std::shared_ptr<Texture2D>& outputImage,
	Remodulation mode,
	std::shared_ptr<UniformBlock>& curCameraUBO,
	std::shared_ptr<UniformBlock>& prevCameraUBO
)
{
	if (!cmd || !lowResImage || lowResImage->IsEmpty() || !outputImage || outputImage->IsEmpty())
		return;

	auto size = outputImage->GetSize();

	// 低分辨率结果刚被空域滤波以 storage 写完：强制一道写 → 读的屏障（同为 GENERAL 时绑定不会自动发）
	lowResImage->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
	outputImage->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	ComputeBindingRecord binding;
	binding.SetCameraUnifromData(curCameraUBO, prevCameraUBO);
	binding.SetStorageImage(outputImage, vk::ImageAspectFlagBits::eColor, 1);
	binding.SetUniformTexture(lowResImage, vk::ImageAspectFlagBits::eColor, 2);
	binding.SetUniformTexture(normalMap, vk::ImageAspectFlagBits::eColor, 3);
	binding.SetUniformTexture(depthMap, vk::ImageAspectFlagBits::eDepth, 4);
	binding.SetUniformTexture(albedoMap, vk::ImageAspectFlagBits::eColor, 5);
	binding.SetUniformTexture(metallicRoughnessMap, vk::ImageAspectFlagBits::eColor, 6);
	binding.SetUniformTexture(positionMap, vk::ImageAspectFlagBits::eColor, 7);

	UpsamplePushConstant pc{ .mode = static_cast<uint32_t>(mode) };
	_pipeline.SetPushConstants(cmd, &pc, sizeof(pc));
	_pipeline.Bind(cmd, binding);
	cmd->dispatch(
		(size.x + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X,
		(size.y + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y,
		1);
}

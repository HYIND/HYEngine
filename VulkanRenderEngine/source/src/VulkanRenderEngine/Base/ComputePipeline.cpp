#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/ComputePipeline.h"


ComputePipelineConfig& ComputePipelineConfig::AddStorageImage(uint32_t binding, uint32_t set)
{
	AddDescriptor(binding, vk::DescriptorType::eStorageImage, defaultShaderStageFlags, 1, set);
	return *this;
}

ComputePipelineConfig& ComputePipelineConfig::AddStorageImageArray(uint32_t binding, uint32_t count, uint32_t set)
{
	AddDescriptor(binding, vk::DescriptorType::eStorageImage, defaultShaderStageFlags, count, set);
	return *this;
}

ComputePipelineConfig& ComputePipelineConfig::AddStorageVariableImageArray(uint32_t binding, uint32_t maxCount, uint32_t set)
{
	vk::DescriptorBindingFlags flags =
		vk::DescriptorBindingFlagBits::eUpdateAfterBind | vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eVariableDescriptorCount;
	auto& result = AddDescriptor(binding, vk::DescriptorType::eStorageImage, defaultShaderStageFlags, maxCount, set, flags);
	variableEntrys[set].isVariable = true;
	variableEntrys[set].maxCount = maxCount;
	return *this;
}

bool ComputePipelineConfig::Validate() const
{
	if (computePath.empty()) {
		std::cout << std::format("[GraphicsPipelineConfig Error] vertexPath or fragmentPath is emtpy!\n");
		return false;
	}
	return true;
}

ComputePipeline::ComputePipeline() {
	m_bindPoint = vk::PipelineBindPoint::eCompute;
	m_bindStage = ImageLayout::BindStage::Compute;
}

ComputePipeline::~ComputePipeline() {
	Release();
}

bool ComputePipeline::Create(
	const ComputePipelineConfig& config,
	VKCore::VulkanDevice* device
)
{
	Release();

	m_device = device;

	// 创建 PipelineLayout
	if (CreatePipelineLayout(config) != vk::Result::eSuccess) {
		return false;
	}

	// 创建 Compute ShaderModule
	auto code = ReadSPIRVFromSourcePath(config.computePath, ShaderType::Compute, config.defines);
	if (CreateShaderModule(code, m_computeModule) != vk::Result::eSuccess) {
		return false;
	}

	// 创建 Pipeline
	return CreatePipeline(config.flags);
}

bool ComputePipeline::CreatePipeline(vk::PipelineCreateFlags flag) {
	static const char* entry = "main";

	vk::PipelineShaderStageCreateInfo stageInfo = {};
	stageInfo.setStage(vk::ShaderStageFlagBits::eCompute)
		.setModule(m_computeModule)
		.setPName(entry);

	vk::ComputePipelineCreateInfo pipelineInfo;
	pipelineInfo.setFlags(flag)
		.setStage(stageInfo)
		.setLayout(m_layout);

	auto [result, pipeline] = m_device->GetHandle().createComputePipeline(vk::PipelineCache(), pipelineInfo);
	if (result != vk::Result::eSuccess)
	{
		std::cout << std::format("[ComputePipeline] create ComputePipeline fail! Error = {}", to_string(result));
		return false;
	}

	m_pipeline = pipeline;
	return true;
}

void ComputePipeline::Release()
{
	if (m_device)
	{
		if (m_computeModule) m_device->GetHandle().destroyShaderModule(m_computeModule);
	}
	m_computeModule = VK_NULL_HANDLE;

	Pipeline::Release();
}

void ComputePipeline::Bind(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, BindingRecord& bindingRecord) {
	if (!cmdBuffer)
		return;

	Pipeline::Bind(cmdBuffer, bindingRecord);
	cmdBuffer->bindPipeline(vk::PipelineBindPoint::eCompute, m_pipeline);
}

void ComputeBindingRecord::SetStorageImage(const std::shared_ptr<Texture2D>& texture, vk::ImageAspectFlags aspect, uint32_t binding, uint32_t set)
{
	SetStorageImage(texture, aspect, BindingPoint{ .binding = binding, .set = set });
}

void ComputeBindingRecord::SetStorageImageLevel(const std::shared_ptr<Texture2D>& texture, vk::ImageAspectFlags aspect, uint32_t baseLevel, uint32_t levelCount, uint32_t binding, uint32_t set)
{
	SetStorageImageLevel(texture, aspect, baseLevel, levelCount, BindingPoint{ .binding = binding, .set = set });
}

void ComputeBindingRecord::SetStorageImageArray(const std::vector<StorageImageEntry>& entrys, uint32_t binding, uint32_t set)
{
	SetStorageImageArray(entrys, BindingPoint{ .binding = binding, .set = set });
}

void ComputeBindingRecord::SetStorageImage(const std::shared_ptr<Texture2D>& texture, vk::ImageAspectFlags aspect, const BindingPoint& bp)
{
	if (!texture)
		return;
	BindingEntry entry{ .type = BindingEntry::DataType::StorageImage, .data = StorageImageEntry{.texture = texture, .aspect = aspect, .baseLevel = 0, .levelCount = UINT32_MAX } };
	m_bindingData[bp] = entry;
}

void ComputeBindingRecord::SetStorageImageLevel(const std::shared_ptr<Texture2D>& texture, vk::ImageAspectFlags aspect, uint32_t baseLevel, uint32_t levelCount, const BindingPoint& bp)
{
	if (!texture)
		return;
	BindingEntry entry{ .type = BindingEntry::DataType::StorageImage, .data = StorageImageEntry{.texture = texture, .aspect = aspect, .baseLevel = baseLevel, .levelCount = levelCount } };
	m_bindingData[bp] = entry;
}

void ComputeBindingRecord::SetStorageImageArray(const std::vector<StorageImageEntry>& entrys, const BindingPoint& bp)
{
	BindingEntry entry{ .type = BindingEntry::DataType::StorageImageArray, .data = StorageImageArrayEntry{.entrys = entrys } };
	m_bindingData[bp] = entry;
}

#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/RayTracingPipeline.h"

inline static uint32_t align_up(uint32_t value, uint32_t alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

RayTracingPipelineConfig& RayTracingPipelineConfig::AddAccelerationStructure(uint32_t binding, uint32_t set)
{
	AddDescriptor(binding, vk::DescriptorType::eAccelerationStructureKHR, defaultShaderStageFlags, 1, set);
	return *this;
}

RayTracingPipelineConfig& RayTracingPipelineConfig::AddStorageImage(uint32_t binding, uint32_t set)
{
	AddDescriptor(binding, vk::DescriptorType::eStorageImage, defaultShaderStageFlags, 1, set);
	return *this;
}

RayTracingPipelineConfig& RayTracingPipelineConfig::AddStorageImageArray(uint32_t binding, uint32_t count, uint32_t set)
{
	AddDescriptor(binding, vk::DescriptorType::eStorageImage, defaultShaderStageFlags, count, set);
	return *this;
}

RayTracingPipelineConfig& RayTracingPipelineConfig::AddStorageVariableImageArray(uint32_t binding, uint32_t maxCount, uint32_t set)
{
	vk::DescriptorBindingFlags flags =
		vk::DescriptorBindingFlagBits::eUpdateAfterBind | vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eVariableDescriptorCount;
	auto& result = AddDescriptor(binding, vk::DescriptorType::eStorageImage, defaultShaderStageFlags, maxCount, set, flags);
	variableEntrys[set].isVariable = true;
	variableEntrys[set].maxCount = maxCount;
	return *this;
}

bool RayTracingPipelineConfig::Validate() const
{
	if (raygenPath.empty()) {
		std::cout << std::format("[RayTracingPipelineConfig Error] raygenPath is emtpy!\n");
		return false;
	}
	if (missPath.empty()) {
		std::cout << std::format("[RayTracingPipelineConfig Error] missPath is emtpy!\n");
		return false;
	}
	if (closestHitPath.empty()) {
		std::cout << std::format("[RayTracingPipelineConfig Error] closestHitPath is emtpy!\n");
		return false;
	}
	return true;
}

RayTracingPipeline::RayTracingPipeline() {
	m_bindPoint = vk::PipelineBindPoint::eRayTracingKHR;
	m_bindStage = Texture2D::BindStage::RayTracing;
	_sbtBufferBlock = std::make_shared<SBTBufferBlock>();
}

RayTracingPipeline::~RayTracingPipeline() {
	Release();
}

bool RayTracingPipeline::Create(
	const RayTracingPipelineConfig& config,
	VKCore::VulkanDevice* device
)
{
	Release();

	m_device = device;

	// 创建 PipelineLayout
	if (CreatePipelineLayout(config) != vk::Result::eSuccess) {
		return false;
	}

	// 创建 Pipeline
	return CreatePipeline(config);
}

bool RayTracingPipeline::CreateShaderStages(const RayTracingPipelineConfig& config)
{
	static auto GetStageCreateInfo = [](vk::ShaderModule module, vk::ShaderStageFlagBits stage) -> vk::PipelineShaderStageCreateInfo
		{
			static const char* entry = "main";
			vk::PipelineShaderStageCreateInfo createInfo;
			createInfo.setStage(stage)
				.setModule(module)
				.setPName(entry);
			return createInfo;
		};

	uint32_t index = 0;
	uint32_t closestHitIndex = VK_SHADER_UNUSED_KHR;
	uint32_t anyHitIndex = VK_SHADER_UNUSED_KHR;
	uint32_t intersectionIndex = VK_SHADER_UNUSED_KHR;

	if (!config.raygenPath.empty()) {
		auto code = ReadSPIRVFromSourcePath(config.raygenPath, ShaderType::RayGen, config.defines);
		if (CreateShaderModule(code, m_raygenModule) != vk::Result::eSuccess) return false;
		m_shaderStages.push_back(GetStageCreateInfo(m_raygenModule, vk::ShaderStageFlagBits::eRaygenKHR));

		vk::RayTracingShaderGroupCreateInfoKHR group{};
		group
			.setType(vk::RayTracingShaderGroupTypeKHR::eGeneral)
			.setGeneralShader(index++)
			.setClosestHitShader(VK_SHADER_UNUSED_KHR)
			.setAnyHitShader(VK_SHADER_UNUSED_KHR)
			.setIntersectionShader(VK_SHADER_UNUSED_KHR);
		m_groupInfos.push_back(group);
	}

	if (!config.missPath.empty()) {
		auto code = ReadSPIRVFromSourcePath(config.missPath, ShaderType::Miss, config.defines);
		if (CreateShaderModule(code, m_missModule) != vk::Result::eSuccess) return false;
		m_shaderStages.push_back(GetStageCreateInfo(m_missModule, vk::ShaderStageFlagBits::eMissKHR));

		vk::RayTracingShaderGroupCreateInfoKHR group{};
		group
			.setType(vk::RayTracingShaderGroupTypeKHR::eGeneral)
			.setGeneralShader(index++)
			.setClosestHitShader(VK_SHADER_UNUSED_KHR)
			.setAnyHitShader(VK_SHADER_UNUSED_KHR)
			.setIntersectionShader(VK_SHADER_UNUSED_KHR);
		m_groupInfos.push_back(group);
	}

	if (!config.closestHitPath.empty()) {
		auto code = ReadSPIRVFromSourcePath(config.closestHitPath, ShaderType::ClosestHit, config.defines);
		if (CreateShaderModule(code, m_closestHitModule) != vk::Result::eSuccess) return false;
		m_shaderStages.push_back(GetStageCreateInfo(m_closestHitModule, vk::ShaderStageFlagBits::eClosestHitKHR));

		closestHitIndex = index++;
	}

	if (!config.anyHitPath.empty()) {
		auto code = ReadSPIRVFromSourcePath(config.anyHitPath, ShaderType::AnyHit, config.defines);
		if (CreateShaderModule(code, m_anyHitModule) != vk::Result::eSuccess) return false;
		m_shaderStages.push_back(GetStageCreateInfo(m_anyHitModule, vk::ShaderStageFlagBits::eAnyHitKHR));

		anyHitIndex = index++;
	}

	if (!config.intersectionPath.empty()) {
		auto code = ReadSPIRVFromSourcePath(config.intersectionPath, ShaderType::Intersection, config.defines);
		if (CreateShaderModule(code, m_intersectionPath) != vk::Result::eSuccess) return false;
		m_shaderStages.push_back(GetStageCreateInfo(m_intersectionPath, vk::ShaderStageFlagBits::eIntersectionKHR));

		intersectionIndex = index++;
	}

	if (closestHitIndex != VK_SHADER_UNUSED_KHR ||
		anyHitIndex != VK_SHADER_UNUSED_KHR ||
		intersectionIndex != VK_SHADER_UNUSED_KHR)
	{
		vk::RayTracingShaderGroupCreateInfoKHR group{};

		if (intersectionIndex == VK_SHADER_UNUSED_KHR) {
			group.setType(vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup);
		}
		else {
			group.setType(vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup);
		}

		group.setGeneralShader(VK_SHADER_UNUSED_KHR)
			.setClosestHitShader(closestHitIndex)
			.setAnyHitShader(anyHitIndex)
			.setIntersectionShader(intersectionIndex);

		m_groupInfos.push_back(group);
	}

	if (!config.callablePath.empty()) {
		auto code = ReadSPIRVFromSourcePath(config.callablePath, ShaderType::Callable, config.defines);
		if (CreateShaderModule(code, m_callableModule) != vk::Result::eSuccess) return false;
		m_shaderStages.push_back(GetStageCreateInfo(m_callableModule, vk::ShaderStageFlagBits::eCallableKHR));

		vk::RayTracingShaderGroupCreateInfoKHR group{};
		group
			.setType(vk::RayTracingShaderGroupTypeKHR::eGeneral)
			.setGeneralShader(index++)
			.setClosestHitShader(VK_SHADER_UNUSED_KHR)
			.setAnyHitShader(VK_SHADER_UNUSED_KHR)
			.setIntersectionShader(VK_SHADER_UNUSED_KHR);
		m_groupInfos.push_back(group);
	}

	return true;
}

bool RayTracingPipeline::CreatePipeline(const RayTracingPipelineConfig& config)
{
	if (!CreateShaderStages(config)) {
		return false;
	}

	vk::RayTracingPipelineCreateInfoKHR pipelineInfo;
	pipelineInfo
		.setFlags(config.flags)
		.setMaxPipelineRayRecursionDepth(2)
		.setStages(m_shaderStages)
		.setLayout(m_layout)
		.setGroups(m_groupInfos);

	auto [result, pipeline] = m_device->GetHandle().createRayTracingPipelineKHR(VK_NULL_HANDLE, vk::PipelineCache(), pipelineInfo);
	if (result != vk::Result::eSuccess)
	{
		std::cout << std::format("[RayTracingPipeline] create RayTracingPipeline fail! Error = {}", to_string(result));
		return false;
	}

	m_pipeline = pipeline;

	auto& rtPipelineProps = m_device->GetVulkanPhysicalDeviceInfo().rtPipelineProps;

	uint32_t groupCount = static_cast<uint32_t>(m_groupInfos.size());
	uint32_t handleSize = rtPipelineProps.shaderGroupHandleSize;		// 每个句柄的大小，从设备属性查询
	std::vector<uint8_t> shaderHandles(groupCount * handleSize);		// 分配存储空间

	// 取出句柄
	m_device->GetHandle().getRayTracingShaderGroupHandlesKHR(
		m_pipeline,
		0,                // firstGroup
		groupCount,
		shaderHandles.size(),
		shaderHandles.data()
	);

	uint32_t handleAlignment = rtPipelineProps.shaderGroupHandleAlignment;
	uint32_t baseAlignment = rtPipelineProps.shaderGroupBaseAlignment;

	// 每个 group 占用的对齐后大小
	uint32_t groupSize = align_up(handleSize, baseAlignment);
	uint32_t regionSize = align_up(groupSize * groupCount, baseAlignment);

	// 创建 SBT buffer
	_sbtBufferBlock->SetSize(regionSize);

	for (uint32_t i = 0; i < groupCount; i++)
		_sbtBufferBlock->WriteData(shaderHandles.data() + i * handleSize, handleSize, i * groupSize);

	VkDeviceAddress sbtAddress = _sbtBufferBlock->GetDeviceAddress();

	_regions.raygenRegion.deviceAddress = sbtAddress + 0 * groupSize;
	_regions.raygenRegion.stride = groupSize;
	_regions.raygenRegion.size = groupSize;

	_regions.missRegion.deviceAddress = sbtAddress + 1 * groupSize;
	_regions.missRegion.stride = groupSize;
	_regions.missRegion.size = groupSize;

	_regions.hitRegion.deviceAddress = sbtAddress + 2 * groupSize;
	_regions.hitRegion.stride = groupSize;
	_regions.hitRegion.size = groupSize;

	if (m_groupInfos.size() >= 4)
	{
		_regions.callableRegion.deviceAddress = sbtAddress + 3 * groupSize;
		_regions.callableRegion.stride = groupSize;
		_regions.callableRegion.size = groupSize;
	}
	else
	{
		_regions.callableRegion.deviceAddress = 0;
		_regions.callableRegion.stride = 0;
		_regions.callableRegion.size = 0;
	}

	return true;
}

void RayTracingPipeline::Release()
{
	if (m_device)
	{
		if (m_raygenModule) m_device->GetHandle().destroyShaderModule(m_raygenModule);
		if (m_missModule) m_device->GetHandle().destroyShaderModule(m_missModule);
		if (m_closestHitModule) m_device->GetHandle().destroyShaderModule(m_closestHitModule);
		if (m_anyHitModule) m_device->GetHandle().destroyShaderModule(m_anyHitModule);
		if (m_intersectionPath) m_device->GetHandle().destroyShaderModule(m_intersectionPath);
		if (m_callableModule) m_device->GetHandle().destroyShaderModule(m_callableModule);

	}

	m_raygenModule = VK_NULL_HANDLE;
	m_missModule = VK_NULL_HANDLE;
	m_closestHitModule = VK_NULL_HANDLE;
	m_anyHitModule = VK_NULL_HANDLE;
	m_intersectionPath = VK_NULL_HANDLE;
	m_callableModule = VK_NULL_HANDLE;

	m_shaderStages.clear();
	m_groupInfos.clear();

	Pipeline::Release();
}

void RayTracingPipeline::Bind(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, BindingRecord& bindingRecord) {
	if (!cmdBuffer)
		return;

	Pipeline::Bind(cmdBuffer, bindingRecord);
	cmdBuffer->bindPipeline(m_bindPoint, m_pipeline);
}

SBTRegionData RayTracingPipeline::GetSBTData() const
{
	return _regions;
}

void RayTracingBindingRecord::SetStorageImageArray(const std::vector<StorageImageEntry>& entrys, uint32_t binding, uint32_t set)
{
	SetStorageImageArray(entrys, BindingPoint{ .binding = binding, .set = set });
}

void RayTracingBindingRecord::SetAccelerationStructure(const vk::AccelerationStructureKHR& handle, uint32_t binding, uint32_t set)
{
	SetAccelerationStructure(handle, BindingPoint{ .binding = binding, .set = set });
}

void RayTracingBindingRecord::SetStorageImage(const std::shared_ptr<Texture2D>& texture, uint32_t binding, uint32_t set)
{
	SetStorageImage(texture, BindingPoint{ .binding = binding, .set = set });
}

void RayTracingBindingRecord::SetStorageImageLevel(const std::shared_ptr<Texture2D>& texture, uint32_t baseLevel, uint32_t levelCount, uint32_t binding, uint32_t set)
{
	SetStorageImageLevel(texture, baseLevel, levelCount, BindingPoint{ .binding = binding, .set = set });
}

void RayTracingBindingRecord::SetStorageImage(const std::shared_ptr<Texture2D>& texture, const BindingPoint& bp)
{
	if (!texture)
		return;
	BindingEntry entry{ .type = BindingEntry::DataType::StorageImage, .data = StorageImageEntry{.texture = texture,.usage = Texture2D::BindUsage::Sample, .baseLevel = 0, .levelCount = UINT32_MAX } };
	m_bindingData[bp] = entry;
}

void RayTracingBindingRecord::SetStorageImageLevel(const std::shared_ptr<Texture2D>& texture, uint32_t baseLevel, uint32_t levelCount, const BindingPoint& bp)
{
	if (!texture)
		return;
	BindingEntry entry{ .type = BindingEntry::DataType::StorageImage, .data = StorageImageEntry{.texture = texture,.usage = Texture2D::BindUsage::Sample, .baseLevel = baseLevel, .levelCount = levelCount } };
	m_bindingData[bp] = entry;
}

void RayTracingBindingRecord::SetStorageImageArray(const std::vector<StorageImageEntry>& entrys, const BindingPoint& bp)
{
	BindingEntry entry{ .type = BindingEntry::DataType::StorageImageArray, .data = StorageImageArrayEntry{.entrys = entrys } };
	m_bindingData[bp] = entry;
}

void RayTracingBindingRecord::SetAccelerationStructure(const vk::AccelerationStructureKHR& handle, const BindingPoint& bp)
{
	if (handle == VK_NULL_HANDLE)
		return;
	BindingEntry entry{ .type = BindingEntry::DataType::AccelerationStructure, .data = AccelerationStructureEntry{.handle = handle } };
	m_bindingData[bp] = entry;
}

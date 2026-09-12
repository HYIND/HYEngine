#pragma once
#include "Pipeline.h"

struct RayTracingPipelineConfig :public PipelineConfig
{
	std::string raygenPath;      // .rgen  - 光线生成
	std::string missPath;        // .rmiss - 未命中
	std::string closestHitPath;  // .rchit - 最近命中
	std::string anyHitPath;      // .rahit - 任意命中（可选）
	std::string intersectionPath;// .rint  - 自定义相交（可选）
	std::string callablePath;    // .rcall - 可调用（可选）

	vk::PipelineCreateFlags flags = {};

	RayTracingPipelineConfig()
	{
		defaultShaderStageFlags = vk::ShaderStageFlagBits::eRaygenKHR
			| vk::ShaderStageFlagBits::eClosestHitKHR
			| vk::ShaderStageFlagBits::eMissKHR
			| vk::ShaderStageFlagBits::eAnyHitKHR
			| vk::ShaderStageFlagBits::eIntersectionKHR
			| vk::ShaderStageFlagBits::eCallableKHR;
	}

	RayTracingPipelineConfig& AddAccelerationStructure(uint32_t binding, uint32_t set = 0);
	RayTracingPipelineConfig& AddStorageImage(uint32_t binding, uint32_t set = 0);
	RayTracingPipelineConfig& AddStorageImageArray(uint32_t binding, uint32_t count = 1, uint32_t set = 0);

	//可变纹理数组，用来支持bindless纹理，必须放在最后一个binding！同一个set中必须最后添加！
	RayTracingPipelineConfig& AddStorageVariableImageArray(uint32_t binding, uint32_t maxCount = 1, uint32_t set = 0);

	bool Validate() const;


	// 转发父类方法，使其返回子类的引用
	RayTracingPipelineConfig& AddUnifromBuffer(uint32_t binding, uint32_t set = 0) { PipelineConfig::AddUnifromBuffer(binding, set); return *this; }
	RayTracingPipelineConfig& AddStorageBuffer(uint32_t binding, uint32_t set = 0) { PipelineConfig::AddStorageBuffer(binding, set); return *this; }
	RayTracingPipelineConfig& AddUnifromTexture(uint32_t binding, uint32_t set = 0) { PipelineConfig::AddUnifromTexture(binding, set); return *this; }
	RayTracingPipelineConfig& AddUnifromBufferArray(uint32_t binding, uint32_t count = 1, uint32_t set = 0) { PipelineConfig::AddUnifromBufferArray(binding, count, set); return *this; }
	RayTracingPipelineConfig& AddUnifromTextureArray(uint32_t binding, uint32_t count = 1, uint32_t set = 0) { PipelineConfig::AddUnifromTextureArray(binding, count, set); return *this; }
	RayTracingPipelineConfig& AddUnifromVariableTextureArray(uint32_t binding, uint32_t maxCount = 1, uint32_t set = 0) { PipelineConfig::AddUnifromVariableTextureArray(binding, maxCount, set); return *this; }
	RayTracingPipelineConfig& AddCameraUnifromDataBinding() { PipelineConfig::AddCameraUnifromDataBinding(); return *this; }
	RayTracingPipelineConfig& AddBindlessMaterialTextureBinding() { PipelineConfig::AddBindlessMaterialTextureBinding(); return *this; }
	RayTracingPipelineConfig& AddLightDataBinding() { PipelineConfig::AddLightDataBinding(); return *this; }
	RayTracingPipelineConfig& AddAnimationDataBinding() { PipelineConfig::AddAnimationDataBinding(); return *this; }
};

struct SBTRegionData
{
	VkStridedDeviceAddressRegionKHR raygenRegion;
	VkStridedDeviceAddressRegionKHR missRegion;
	VkStridedDeviceAddressRegionKHR hitRegion;
	VkStridedDeviceAddressRegionKHR callableRegion;
};

class RayTracingPipeline : public Pipeline {
public:
	RayTracingPipeline();
	~RayTracingPipeline() override;

	bool Create(
		const RayTracingPipelineConfig& config,
		VKCore::VulkanDevice* device = VKCONTEXT->GetDevice().get()
	);

	virtual void Release() override;
	virtual void Bind(std::shared_ptr<VKWrapper::VKCommandBuffer> cmdBuffer) override;

	SBTRegionData GetSBTData() const;

	void SetStorageImage(const std::shared_ptr<Texture2D>& texture, uint32_t binding, uint32_t set = 0);
	void SetStorageImageLevel(const std::shared_ptr<Texture2D>& texture, uint32_t baseLevel, uint32_t levelCount, uint32_t binding, uint32_t set = 0);
	void SetStorageImageArray(const std::vector<StorageImageEntry>& entrys, uint32_t binding, uint32_t set = 0);
	void SetAccelerationStructure(const vk::AccelerationStructureKHR& handle, uint32_t binding, uint32_t set = 0);


	void SetStorageImage(const std::shared_ptr<Texture2D>& texture, const BindingPoint& bp);
	void SetStorageImageLevel(const std::shared_ptr<Texture2D>& texture, uint32_t baseLevel, uint32_t levelCount, const BindingPoint& bp);
	void SetStorageImageArray(const std::vector<StorageImageEntry>& entrys, const BindingPoint& bp);
	void SetAccelerationStructure(const vk::AccelerationStructureKHR& handle, const BindingPoint& bp);

private:
	bool CreatePipeline(const RayTracingPipelineConfig& config);
	bool CreateShaderStages(const RayTracingPipelineConfig& config);

private:
	vk::ShaderModule m_raygenModule = VK_NULL_HANDLE;
	vk::ShaderModule m_missModule = VK_NULL_HANDLE;
	vk::ShaderModule m_closestHitModule = VK_NULL_HANDLE;
	vk::ShaderModule m_anyHitModule = VK_NULL_HANDLE;
	vk::ShaderModule m_intersectionPath = VK_NULL_HANDLE;
	vk::ShaderModule m_callableModule = VK_NULL_HANDLE;

	std::vector<vk::PipelineShaderStageCreateInfo> m_shaderStages;
	std::vector<vk::RayTracingShaderGroupCreateInfoKHR> m_groupInfos;

	std::shared_ptr<SBTBufferBlock> _sbtBufferBlock;
	SBTRegionData _regions;
};

#pragma once
#include "Pipeline.h"

struct ComputePipelineConfig :public PipelineConfig
{
	std::string computePath;
	vk::PipelineCreateFlags flags = {};

	ComputePipelineConfig()
	{
		defaultShaderStageFlags = vk::ShaderStageFlagBits::eCompute;
	}

	ComputePipelineConfig& AddStorageImage(uint32_t binding, uint32_t set = 0);
	ComputePipelineConfig& AddStorageImageArray(uint32_t binding, uint32_t count = 1, uint32_t set = 0);

	//可变纹理数组，用来支持bindless纹理，必须放在最后一个binding！同一个set中必须最后添加！
	ComputePipelineConfig& AddStorageVariableImageArray(uint32_t binding, uint32_t maxCount = 1, uint32_t set = 0);

	bool Validate() const;


	// 转发父类方法，使其返回子类的引用
	ComputePipelineConfig& AddUnifromBuffer(uint32_t binding, uint32_t set = 0) { PipelineConfig::AddUnifromBuffer(binding, set); return *this; }
	ComputePipelineConfig& AddStorageBuffer(uint32_t binding, uint32_t set = 0) { PipelineConfig::AddStorageBuffer(binding, set); return *this; }
	ComputePipelineConfig& AddUnifromTexture(uint32_t binding, uint32_t set = 0) { PipelineConfig::AddUnifromTexture(binding, set); return *this; }
	ComputePipelineConfig& AddUnifromBufferArray(uint32_t binding, uint32_t count = 1, uint32_t set = 0) { PipelineConfig::AddUnifromBufferArray(binding, count, set); return *this; }
	ComputePipelineConfig& AddUnifromTextureArray(uint32_t binding, uint32_t count = 1, uint32_t set = 0) { PipelineConfig::AddUnifromTextureArray(binding, count, set); return *this; }
	ComputePipelineConfig& AddUnifromVariableTextureArray(uint32_t binding, uint32_t maxCount = 1, uint32_t set = 0) { PipelineConfig::AddUnifromVariableTextureArray(binding, maxCount, set); return *this; }
	ComputePipelineConfig& AddCameraUnifromDataBinding() { PipelineConfig::AddCameraUnifromDataBinding(); return *this; }
	ComputePipelineConfig& AddBindlessMaterialTextureBinding() { PipelineConfig::AddBindlessMaterialTextureBinding(); return *this; }
	ComputePipelineConfig& AddLightDataBinding() { PipelineConfig::AddLightDataBinding(); return *this; }
	ComputePipelineConfig& AddAnimationDataBinding() { PipelineConfig::AddAnimationDataBinding(); return *this; }
};

class ComputePipeline : public Pipeline {
public:
	ComputePipeline();
	~ComputePipeline() override;

	bool Create(
		const ComputePipelineConfig& config,
		VKCore::VulkanDevice* device = VKCONTEXT->GetDevice().get()
	);

	virtual void Release() override;
	virtual void Bind(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, BindingRecord& bindingRecord) override;

private:
	bool CreatePipeline(vk::PipelineCreateFlags flag = {});

private:
	vk::ShaderModule m_computeModule = VK_NULL_HANDLE;
};

class ComputeBindingRecord :public BindingRecord
{
public:
	ComputeBindingRecord() = default;

	void SetStorageImage(const std::shared_ptr<Texture2D>& texture, uint32_t binding, uint32_t set = 0);
	void SetStorageImageLevel(const std::shared_ptr<Texture2D>& texture, uint32_t baseLevel, uint32_t levelCount, uint32_t binding, uint32_t set = 0);
	void SetStorageImageArray(const std::vector<BindingRecord::StorageImageEntry>& entrys, uint32_t binding, uint32_t set = 0);

	void SetStorageImage(const std::shared_ptr<Texture2D>& texture, const BindingPoint& bp);
	void SetStorageImageLevel(const std::shared_ptr<Texture2D>& texture, uint32_t baseLevel, uint32_t levelCount, const BindingPoint& bp);
	void SetStorageImageArray(const std::vector<BindingRecord::StorageImageEntry>& entrys, const BindingPoint& bp);
};

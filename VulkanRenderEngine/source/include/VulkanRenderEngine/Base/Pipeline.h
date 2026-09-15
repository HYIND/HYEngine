#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine\VKCore\VulkanDevice.h"
#include "VulkanRenderEngine\VKWrapper\WrapperGeneral.h"
#include "Texture2D.h"
#include "TextureCube.h"
#include "DynamicBlock.h"

template<typename T>
concept StringConvertable = !std::is_same_v<T, std::string>
&& requires(const T& value) {
	{ std::to_string(value) } -> std::convertible_to<std::string>;
};

enum class ShaderType {
	Vertex = 0,
	Geometry,
	Fragment,
	Compute,
	RayGen,
	Miss,
	ClosestHit,
	AnyHit,
	Intersection,
	Callable
};

struct PipelineConfig {

	struct VariableEntry {
		bool isVariable = false;
		uint32_t maxCount = 0;
	};

	vk::ShaderStageFlags defaultShaderStageFlags;

	// ---------- 从源码编译时可选的动态宏定义 ----------
	std::map<std::string, std::string> defines;

	template <StringConvertable T>
	void AddDefineMacro(const std::string& name, const T& value) { AddDefineMacro(name, std::to_string(value)); }
	void AddDefineMacro(const std::string& name, const std::string& value);
	void RemoveDefineMarco(const std::string& name);

	// ---------- 资源布局 ----------
	std::vector<std::vector<vk::DescriptorSetLayoutBinding>> descriptorSetLayouts;
	std::vector<std::vector<vk::DescriptorBindingFlags>> bindingFlags;
	std::vector<VariableEntry> variableEntrys;
	std::vector<uint32_t> pushConstantSizes;

	PipelineConfig& AddUnifromBuffer(uint32_t binding, uint32_t set = 0);
	PipelineConfig& AddStorageBuffer(uint32_t binding, uint32_t set = 0);

	PipelineConfig& AddUnifromTexture(uint32_t binding, uint32_t set = 0);

	PipelineConfig& AddUnifromBufferArray(uint32_t binding, uint32_t count = 1, uint32_t set = 0);
	PipelineConfig& AddUnifromTextureArray(uint32_t binding, uint32_t count = 1, uint32_t set = 0);

	//可变纹理数组，用来支持bindless纹理，必须放在最后一个binding！同一个set中必须最后添加！
	PipelineConfig& AddUnifromVariableTextureArray(uint32_t binding, uint32_t maxCount = 1, uint32_t set = 0);

	PipelineConfig& AddDescriptor(uint32_t binding,
		vk::DescriptorType type, vk::ShaderStageFlags stageflags,
		uint32_t count = 1, uint32_t set = 0,
		vk::DescriptorBindingFlags flags = vk::DescriptorBindingFlagBits::eUpdateAfterBind | vk::DescriptorBindingFlagBits::ePartiallyBound
	);

	PipelineConfig& AddCameraUnifromDataBinding();
	PipelineConfig& AddBindlessMaterialTextureBinding();
	PipelineConfig& AddLightDataBinding();
	PipelineConfig& AddAnimationDataBinding();

	void AddPushConstant(uint32_t size);	// 添加推送常量范围（自动处理偏移量）
};

struct BindingPoint
{
	uint32_t binding;
	uint32_t set;
	bool operator==(const BindingPoint& other) const {
		return binding == other.binding && set == other.set;
	}
};
namespace std {
	template<>
	struct hash<BindingPoint> {
		size_t operator()(const BindingPoint& bp) const noexcept {
			uint64_t combined = (static_cast<uint64_t>(bp.set) << 32) | bp.binding;
			return std::hash<uint64_t>{}(combined);
		}
	};
}

namespace GeneralBindingPoint
{
	static const BindingPoint Camera_Cur = BindingPoint{ .binding = 0, .set = 1 };
	static const BindingPoint Camera_Prev = BindingPoint{ .binding = 1, .set = 1 };

	static const BindingPoint Material_materials = BindingPoint{ .binding = 0, .set = 2 };
	static const BindingPoint Material_textures = BindingPoint{ .binding = 1, .set = 2 };

	static const BindingPoint Light_DirLightMetaData = BindingPoint{ .binding = 0, .set = 3 };
	static const BindingPoint Light_DirLightCascadeData = BindingPoint{ .binding = 1, .set = 3 };
	static const BindingPoint Light_PointLightMetaData = BindingPoint{ .binding = 2, .set = 3 };
	static const BindingPoint Light_SpotLightMetaData = BindingPoint{ .binding = 3, .set = 3 };

	static const BindingPoint Animation_MetaData = BindingPoint{ .binding = 0, .set = 4 };
	static const BindingPoint Animation_MatData = BindingPoint{ .binding = 1, .set = 4 };
	static const BindingPoint Animation_PrevMetaData = BindingPoint{ .binding = 2, .set = 4 };
	static const BindingPoint Animation_PrevMatData = BindingPoint{ .binding = 3, .set = 4 };
}

class Pipeline
{
private:
	struct DescriptorSetLayoutData;
	struct DescriptorSetGroup;

	class DescriptorSetGroupPool
	{

	public:
		DescriptorSetGroupPool(uint32_t maxResNum = 50);
		void Clear();
		std::shared_ptr<DescriptorSetGroup> Fetch();
		bool Recycle(std::shared_ptr<DescriptorSetGroup> setGroup);	// 回收

	private:
		std::unordered_set<std::shared_ptr<DescriptorSetGroup>> _iDleList;
		std::unordered_set<std::shared_ptr<DescriptorSetGroup>> _datas;
		uint32_t _maxResNum = 50;
	};
	struct DescriptorSetGroupHolder
	{
		std::weak_ptr<DescriptorSetLayoutData> parent;
		std::shared_ptr<DescriptorSetGroup> data;
		DescriptorSetGroupHolder() {}
		DescriptorSetGroupHolder(std::weak_ptr<DescriptorSetLayoutData> parent, std::shared_ptr<DescriptorSetGroup> data);
		~DescriptorSetGroupHolder();
	};

public:
	struct DescriptorSetGroup {
		VKCore::VulkanDevice* device;
		vk::DescriptorPool pool;
		std::vector<vk::DescriptorSet> sets;
		DescriptorSetGroup(VKCore::VulkanDevice* device, vk::DescriptorPool pool, const std::vector<vk::DescriptorSet>& sets);
		~DescriptorSetGroup();
	};

	struct DescriptorSetLayoutData :public std::enable_shared_from_this<DescriptorSetLayoutData>
	{
		VKCore::VulkanDevice* device;
		vk::DescriptorPool pool;
		std::vector<vk::DescriptorSetLayout> setLayouts;
		DescriptorSetGroupPool setGroupPool;

		struct SetLayoutInfo {
			bool isNullSet = false;
			bool isVariable = false;
			uint32_t variableCount = 0;
		};
		std::vector<SetLayoutInfo> setLayoutInfos;

		DescriptorSetLayoutData(VKCore::VulkanDevice* device, vk::DescriptorPool pool, const std::vector<vk::DescriptorSetLayout>& setLayouts);
		~DescriptorSetLayoutData();
		bool GetDescriptorSetGroup(DescriptorSetGroupHolder& holder);
	};

public:
	struct UniformBlockEntry
	{
		std::shared_ptr<UniformBlock> block;
		std::shared_ptr<VKWrapper::VmaBuffer> buffer;
	};

	struct StorageBlockEntry
	{
		std::shared_ptr<StorageBlock> block;
		std::shared_ptr<VKWrapper::VmaBuffer> buffer;
	};

	struct UniformTextureEntry
	{
		std::shared_ptr<Texture2D> texture;
		TextureDescBindEntry descEntry;
	};

	struct UniformTextureCubeEntry
	{
		std::shared_ptr<TextureCube> texture;
		TextureCubeDescBindEntry descEntry;
	};

	struct UniformTextureArrayEntry
	{
		std::shared_ptr<ITextureArrayProvider> provider;
		std::vector<TextureDescBindEntry> descEntrys;
	};

	struct StorageImageEntry
	{
		std::shared_ptr<Texture2D> texture;
		Texture2D::BindUsage usage;
		TextureDescBindEntry descEntry;
		uint32_t baseLevel = 0;
		uint32_t levelCount = UINT32_MAX;
	};

	struct StorageImageArrayEntry
	{
		std::vector<StorageImageEntry> entrys;
	};

	struct AccelerationStructureEntry
	{
		vk::AccelerationStructureKHR handle;
	};

	struct BindingEntry
	{
		enum class DataType { UniformBlock = 0, StorageBlock, UniformTex, UniformTexCube, UniformTexArray, StorageImage, StorageImageArray, AccelerationStructure };
		DataType type;
		std::variant<
			UniformBlockEntry, StorageBlockEntry,
			UniformTextureEntry, UniformTextureCubeEntry, UniformTextureArrayEntry,
			StorageImageEntry, StorageImageArrayEntry,
			AccelerationStructureEntry>
			data;
	};

public:
	Pipeline() = default;
	virtual ~Pipeline();

	// 禁止拷贝，允许移动
	Pipeline(const Pipeline&) = delete;
	Pipeline& operator=(const Pipeline&) = delete;
	Pipeline(Pipeline&& other) noexcept;
	Pipeline& operator=(Pipeline&& other) noexcept;

	// ---------- 绑定 ----------
	virtual void Bind(std::shared_ptr<VKWrapper::VKCommandBuffer> cmdBuffer);

	// ---------- Getter ----------
	vk::Pipeline GetHandle() const;
	vk::PipelineLayout GetLayout() const;
	const std::vector<vk::DescriptorSetLayout>& GetDescriptorSetLayout() const;
	VKCore::VulkanDevice* GetDevice() const;
	bool IsValid() const;

	static std::vector<uint32_t> ReadSPIRVFromSourcePath(const std::string& path, ShaderType type, const std::map<std::string, std::string>& defines = {});
	static std::vector<uint32_t> ReadSPIRVFromSourceCode(const std::string& sourceCode, ShaderType type, const std::map<std::string, std::string>& defines = {});
	static std::vector<uint32_t> ReadSPIRVBinaryPath(const std::string& path);

	virtual void Release();

public:
	// UBO、SSBO关联，以下方法仅记录绑定关系，不执行cmd，需调用Bind生效最新的配置

	// 设置绑定点数据，单个绑定点只能绑定一种类型的数据，已有的绑定点会被替换
	void SetUniformBlock(const std::shared_ptr<UniformBlock>& uniformBlock, uint32_t binding, uint32_t set = 0);
	void SetStorageBlock(const std::shared_ptr<StorageBlock>& storageBlock, uint32_t binding, uint32_t set = 0);
	void SetUniformTexture(const std::shared_ptr<Texture2D>& uniformTex, uint32_t binding, uint32_t set = 0);
	void SetUniformTextureCube(const std::shared_ptr<TextureCube>& uniformTexCube, uint32_t binding, uint32_t set = 0);
	void SetUniformTextureArray(const std::shared_ptr<ITextureArrayProvider>& provider, uint32_t binding, uint32_t set = 0);
	void SetUniformTextureArray(const std::vector<std::shared_ptr<Texture2D>>& array, uint32_t binding, uint32_t set = 0);

	// 获取绑定点上的Buffer，绑定点未绑定或者和指定类型不同时，则创建新的Buffer/Texture并绑定
	std::shared_ptr<UniformBlock> GetUniformBlock(uint32_t binding, uint32_t set = 0);
	std::shared_ptr<StorageBlock> GetStorageBlock(uint32_t binding, uint32_t set = 0);

	// 和Get的区别在于不会自动创建
	std::shared_ptr<UniformBlock> FindUniformBlock(uint32_t binding, uint32_t set = 0);
	std::shared_ptr<StorageBlock> FindStorageBlock(uint32_t binding, uint32_t set = 0);
	std::shared_ptr<Texture2D> FindUniformTexture(uint32_t binding, uint32_t set = 0);
	std::shared_ptr<ITextureArrayProvider> FindUniformTextureArray(uint32_t binding, uint32_t set = 0);


	void SetUniformBlock(const std::shared_ptr<UniformBlock>& uniformBlock, const BindingPoint& bp);
	void SetStorageBlock(const std::shared_ptr<StorageBlock>& storageBlock, const BindingPoint& bp);
	void SetUniformTexture(const std::shared_ptr<Texture2D>& uniformTex, const BindingPoint& bp);
	void SetUniformTextureCube(const std::shared_ptr<TextureCube>& uniformTexCube, const BindingPoint& bp);
	void SetUniformTextureArray(const std::shared_ptr<ITextureArrayProvider>& provider, const BindingPoint& bp);
	void SetUniformTextureArray(const std::vector<std::shared_ptr<Texture2D>>& array, const BindingPoint& bp);

	std::shared_ptr<UniformBlock> GetUniformBlock(const BindingPoint& bp);
	std::shared_ptr<StorageBlock> GetStorageBlock(const BindingPoint& bp);

	std::shared_ptr<UniformBlock> FindUniformBlock(const BindingPoint& bp);
	std::shared_ptr<StorageBlock> FindStorageBlock(const BindingPoint& bp);
	std::shared_ptr<Texture2D> FindUniformTexture(const BindingPoint& bp);
	std::shared_ptr<ITextureArrayProvider> FindUniformTextureArray(const BindingPoint& bp);

public:
	void SetCameraUnifromData(const std::shared_ptr<UniformBlock>& curCmaeraUBO, const std::shared_ptr<UniformBlock>& prevCameraUBO);
	void SetBindlessMaterialTexture(const std::shared_ptr<StorageBlock>& materials, const std::shared_ptr<ITextureArrayProvider>& textures);
	void SetLightStorageData(const std::shared_ptr<StorageBlock>& _ssbo_dirLightMeta, const std::shared_ptr<StorageBlock>& _ssbo_dirLightCascade, const std::shared_ptr<StorageBlock>& _ssbo_pointLightMeta, const std::shared_ptr<StorageBlock>& _ssbo_spotLightMeta);

public:
	// 推送常量
	void SetPushConstants(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd, const void* data, uint32_t size, uint32_t offset = 0);

private:
	void BindAllEntry(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, std::shared_ptr<DescriptorSetGroup>& data);
	void BindEntry(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, std::shared_ptr<DescriptorSetGroup>& data, const BindingPoint& point, BindingEntry& entry);
	void BindUniformBlock(UniformBlockEntry& entry, std::shared_ptr<DescriptorSetGroup>& data, uint32_t binding, uint32_t set);
	void BindStorageBlock(StorageBlockEntry& entry, std::shared_ptr<DescriptorSetGroup>& data, uint32_t binding, uint32_t set);
	void BindUniformTexture(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, std::shared_ptr<DescriptorSetGroup>& data, UniformTextureEntry& entry, uint32_t binding, uint32_t set);
	void BindUniformTextureCube(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, std::shared_ptr<DescriptorSetGroup>& data, UniformTextureCubeEntry& entry, uint32_t binding, uint32_t set);
	void BindUniformTextureArray(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, std::shared_ptr<DescriptorSetGroup>& data, UniformTextureArrayEntry& entry, uint32_t binding, uint32_t set);
	void BindStorageImage(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, std::shared_ptr<DescriptorSetGroup>& data, StorageImageEntry& entry, uint32_t binding, uint32_t set);
	void BindStorageImageArray(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, std::shared_ptr<DescriptorSetGroup>& data, StorageImageArrayEntry& entry, uint32_t binding, uint32_t set);
	void BindAccelerationStructure(AccelerationStructureEntry& entry, std::shared_ptr<DescriptorSetGroup>& data, uint32_t binding, uint32_t set);

protected:
	// ---------- 工具函数 ----------
	vk::Result CreateShaderModule(const std::vector<uint32_t>& code, vk::ShaderModule& outModule);
	vk::Result CreatePipelineLayout(const PipelineConfig& config);
	vk::Result CreateDescriptorSetLayout(const std::vector<std::vector<vk::DescriptorSetLayoutBinding>>& bindings, const std::vector<std::vector<vk::DescriptorBindingFlags>>& bindingFlags, std::vector<vk::DescriptorSetLayout>& outLayouts);
	vk::Result CreateDescriptorSets(const std::vector<std::vector<vk::DescriptorBindingFlags>>& bindingFlags, const std::vector<PipelineConfig::VariableEntry>& variableEntrys);

protected:
	// ---------- 成员变量 ----------
	VKCore::VulkanDevice* m_device = nullptr;
	vk::Pipeline m_pipeline = VK_NULL_HANDLE;
	vk::PipelineLayout m_layout = VK_NULL_HANDLE;
	//std::vector<vk::DescriptorSetLayout> m_descriptorSetLayouts;
	//std::vector<vk::DescriptorSet> m_descriptorSets;

	std::shared_ptr<DescriptorSetLayoutData> m_descriptorSetLayouts;

	vk::PipelineBindPoint m_bindPoint;
	Texture2D::BindStage m_bindStage;

	std::string m_debugName;

protected:
	std::unordered_map<BindingPoint, BindingEntry> m_bindingData;
};

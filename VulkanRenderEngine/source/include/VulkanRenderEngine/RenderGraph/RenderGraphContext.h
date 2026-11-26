#pragma once

#include "vkstdafx.h"
#include "../Base/Texture2D.h"
#include "../Base/Texture3D.h"
#include "VulkanRenderEngine/VKWrapper/WrapperGeneral.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"


namespace RenderGraph
{

	struct TextureDesc {
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t depth = 1;					// > 1 表示 3D 体积纹理（走 Texture3D）

		vk::Format format = vk::Format::eR8G8B8A8Unorm;
		vk::Filter minFilter = vk::Filter::eLinear;
		vk::Filter magFilter = vk::Filter::eLinear;
		vk::SamplerAddressMode wrapU = vk::SamplerAddressMode::eClampToEdge;
		vk::SamplerAddressMode wrapV = vk::SamplerAddressMode::eClampToEdge;
		vk::SamplerAddressMode wrapW = vk::SamplerAddressMode::eClampToEdge;	// 仅 3D 生效

		uint32_t maxLevel = 1;
		bool isVariable = false;

		bool IsVolume() const { return depth > 1; }
	};

	class TextureHandle
	{
	public:
		TextureHandle() : _id(0) {}
		explicit TextureHandle(uint32_t id, const TextureDesc& desc) : _id(id), _desc(desc) {}
		uint32_t GetID() const { return _id; }
		TextureDesc GetDesc() const { return _desc; }
		bool IsValid() const { return _id != 0; }
		bool operator==(const TextureHandle& other) const { return _id == other._id; }
		explicit operator bool() const { return _id > 0; }

	private:
		uint32_t _id = 0;
		TextureDesc _desc;
	};

	struct TextureSlot
	{
		using Variant = std::variant<std::monostate, std::shared_ptr<Texture2D>, std::shared_ptr<Texture3D>>;

		Variant data;

		TextureSlot() = default;
		TextureSlot(std::shared_ptr<Texture2D> tex) : data(std::move(tex)) {}
		TextureSlot(std::shared_ptr<Texture3D> tex) : data(std::move(tex)) {}

		bool IsValid() const { return !std::holds_alternative<std::monostate>(data); }
		bool IsVolume() const { return std::holds_alternative<std::shared_ptr<Texture3D>>(data); }

		template<typename T>
		std::shared_ptr<T> As() const
		{
			auto* p = std::get_if<std::shared_ptr<T>>(&data);
			return p ? *p : nullptr;
		}

		template<typename T>
		void Set(std::shared_ptr<T> tex) { data = std::move(tex); }

		void Reset() { data = std::monostate{}; }
	};

	struct PassFrameContext
	{
		std::string passName;

		std::vector<TextureSlot> inputs;
		std::vector<TextureSlot> optionInputs;
		std::vector<TextureSlot> outputs;
		std::vector<TextureSlot> temps;
		std::vector<TextureSlot> persistents;
		std::vector<TextureSlot> optionPersistents;
		std::vector<TextureSlot> externals;
		std::vector<TextureSlot> frameLocals;

		const std::string& GetName() const { return passName; };

		std::shared_ptr<Texture2D> GetInput(uint32_t idx) const { return GetInputAs<Texture2D>(idx); };
		std::shared_ptr<Texture2D> GetOptionalInput(uint32_t idx) const { return GetOptionalInputAs<Texture2D>(idx); };
		std::shared_ptr<Texture2D> GetOutput(uint32_t idx) const { return GetOutputAs<Texture2D>(idx); };
		std::shared_ptr<Texture2D> GetTemp(uint32_t idx) const { return GetTempAs<Texture2D>(idx); };
		std::shared_ptr<Texture2D> GetPersitent(uint32_t idx) const { return GetPersitentAs<Texture2D>(idx); };
		std::shared_ptr<Texture2D> GetOptionalPersitent(uint32_t idx) const { return GetOptionalPersitentAs<Texture2D>(idx); };
		std::shared_ptr<Texture2D> GetExternal(uint32_t idx) const { return GetExternalAs<Texture2D>(idx); };
		std::shared_ptr<Texture2D> GetFrameLocal(uint32_t idx) const { return GetFrameLocalAs<Texture2D>(idx); };

		std::shared_ptr<Texture3D> GetVolumeInput(uint32_t idx) const { return GetInputAs<Texture3D>(idx); };
		std::shared_ptr<Texture3D> GetOptionalVolumeInput(uint32_t idx) const { return GetOptionalInputAs<Texture3D>(idx); };
		std::shared_ptr<Texture3D> GetVolumeOutput(uint32_t idx) const { return GetOutputAs<Texture3D>(idx); };
		std::shared_ptr<Texture3D> GetVolumeTemp(uint32_t idx) const { return GetTempAs<Texture3D>(idx); };
		std::shared_ptr<Texture3D> GetVolumePersitent(uint32_t idx) const { return GetPersitentAs<Texture3D>(idx); };
		std::shared_ptr<Texture3D> GetVolumeExternal(uint32_t idx) const { return GetExternalAs<Texture3D>(idx); };
		std::shared_ptr<Texture3D> GetVolumeFrameLocal(uint32_t idx) const { return GetFrameLocalAs<Texture3D>(idx); };

		template<typename T> std::shared_ptr<T> GetInputAs(uint32_t idx) const { return GetAt<T>(inputs, idx); }
		template<typename T> std::shared_ptr<T> GetOptionalInputAs(uint32_t idx) const { return GetAt<T>(optionInputs, idx); }
		template<typename T> std::shared_ptr<T> GetOutputAs(uint32_t idx) const { return GetAt<T>(outputs, idx); }
		template<typename T> std::shared_ptr<T> GetTempAs(uint32_t idx) const { return GetAt<T>(temps, idx); }
		template<typename T> std::shared_ptr<T> GetPersitentAs(uint32_t idx) const { return GetAt<T>(persistents, idx); }
		template<typename T> std::shared_ptr<T> GetOptionalPersitentAs(uint32_t idx) const { return GetAt<T>(optionPersistents, idx); }
		template<typename T> std::shared_ptr<T> GetExternalAs(uint32_t idx) const { return GetAt<T>(externals, idx); }
		template<typename T> std::shared_ptr<T> GetFrameLocalAs(uint32_t idx) const { return GetAt<T>(frameLocals, idx); }

	private:
		template<typename T>
		static std::shared_ptr<T> GetAt(const std::vector<TextureSlot>& slots, uint32_t idx)
		{
			return static_cast<size_t>(idx) < slots.size() ? slots[idx].As<T>() : nullptr;
		}
	};

	using ResourceName = std::string;

	enum class ResourceType { Texture };
	struct RenderGraphResource
	{
		ResourceName name;
		ResourceType type;
		std::variant<TextureDesc> desc;

		bool operator==(const RenderGraphResource& other) const { return name == other.name && type == other.type; }
		bool operator!=(const RenderGraphResource& other) const { return name != other.name || type != other.type; }
	};

	struct ExternalResource
	{
		ResourceName name;
		ResourceType type;

		bool operator==(const ExternalResource& other) const { return name == other.name && type == other.type; }
		bool operator!=(const ExternalResource& other) const { return name != other.name || type != other.type; }
	};

	struct TextureLayout
	{
		ImageLayout::BindStage stage;
		ImageLayout::BindUsage usage;
	};

	struct RenderGraphResourceLayout
	{
		std::variant<TextureLayout> data;
	};

	class FrameDataRegistry
	{
	public:
		template<typename T>
		void Store(const std::string& name, T&& value) {
			data.insert_or_assign(name, std::forward<T>(value));
		}

		template<typename T>
		void Store(const std::string& name, const T& value) {
			data[name] = value;
		}

		template<typename T>
		T& Load(const std::string& name) {
			auto it = data.find(name);
			if (it == data.end()) {
				throw std::runtime_error("Frame data not found: " + name);
			}
			return std::any_cast<T&>(it->second);
		}

		template<typename T>
		T* TryLoad(const std::string& name) {
			auto it = data.find(name);
			if (it == data.end()) {
				return nullptr;
			}
			return std::any_cast<T>(&it->second);
		}

		// 检查是否存在
		bool Has(const std::string& name) const {
			return data.find(name) != data.end();
		}

		void Clear() {
			data.clear();
		}

		template<typename T>
		T& Get(const std::string& name) {
			auto it = data.find(name);
			if (it == data.end()) {
				it = data.emplace(name, T{}).first;
			}
			return std::any_cast<T&>(it->second);
		}

		template<typename T>
		std::shared_ptr<T> GetShared(const std::string& name) {
			auto it = data.find(name);

			if (it == data.end()) {
				auto ptr = std::make_shared<T>();
				it = data.emplace(name, ptr).first;
				return ptr;
			}

			return std::any_cast<std::shared_ptr<T>>(it->second);
		}

		std::shared_ptr<UniformBlock> GetUniformBlock(const std::string& name) {
			return GetShared<UniformBlock>(name);
		}

		std::shared_ptr<StorageBlock> GetStorageBlock(const std::string& name) {
			return GetShared<StorageBlock>(name);
		}

		std::shared_ptr<VertexBufferBlock> GetVertexBlock(const std::string& name) {
			return GetShared<VertexBufferBlock>(name);
		}

		std::shared_ptr<IndexBufferBlock> GetIndexBlock(const std::string& name) {
			return GetShared<IndexBufferBlock>(name);
		}

		std::shared_ptr<IndirectBufferBlock> GetIndirectBlock(const std::string& name) {
			return GetShared<IndirectBufferBlock>(name);
		}

		std::shared_ptr<SBTBufferBlock> GetSBTBlock(const std::string& name) {
			return GetShared<SBTBufferBlock>(name);
		}

	private:
		std::unordered_map<std::string, std::any> data;
	};


	// 一次 Graph::Execute 的提交上下文（每次 Execute 新建一份）：
	//   mutex    —— 各 Pass 按顺序「补差 + 提交」，保证 CPU 记录的图像状态按提交顺序更新；
	//   timeline —— 提交链：图内每次提交等上一个值、signal 下一个值，GPU 上按提交顺序串行，
	//               semaphore 的等待自带执行依赖和内存依赖，CPU 不必再等每个 Pass 做完
	struct GraphSubmitContext
	{
		CriticalSectionLock mutex;
		std::shared_ptr<VKWrapper::VKTimelineSemaphore> timeline;
		uint64_t value = 0;		// 链上最后一次 signal 的值，只在持有 mutex 时读写

		// 接到链尾提交。成功返回本次 signal 的值；失败返回 0，且不推进 value（否则之后等这个值的都会卡死）
		uint64_t Submit(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
			CmdSyncSeamphore syncSeamphore = {}, std::shared_ptr<VKWrapper::VKFence> signalFence = nullptr);
	};

	class PassFrameCmdContext;
	class PassFrameCmd :public VKWrapper::VKCommandBuffer
	{
	public:
		PassFrameCmd(PassFrameCmdContext* ctx);
		PassFrameCmd(PassFrameCmd&& other);
		virtual ~PassFrameCmd() = default;

		virtual void SubmitToQueue(const CmdSyncSeamphore& syncSeamphore = {}, std::shared_ptr<VKWrapper::VKFence> signalFence = nullptr);
		virtual void SubmitNow(const CmdSyncSeamphore& syncSeamphore = {}, std::shared_ptr<VKWrapper::VKFence> signalFence = nullptr);
		virtual void SubmitNowAndWait(const CmdSyncSeamphore& syncSeamphore = {});

	private:
		PassFrameCmdContext* _ctx = nullptr;
	};

	class PassFrameCmdContext
	{
	private:
		struct TexLayoutData {
			std::shared_ptr<VKWrapper::BaseVKImage> image;
			RenderGraphResourceLayout layout;
		};

	public:
		PassFrameCmdContext(const std::shared_ptr<GraphSubmitContext>& submitCtx);
		~PassFrameCmdContext();

		std::shared_ptr<PassFrameCmd> GetCmd();

		void SubmitToQueue(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, const CmdSyncSeamphore& syncSeamphore = {}, std::shared_ptr<VKWrapper::VKFence> signalFence = nullptr);
		void SubmitNow(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, const CmdSyncSeamphore& syncSeamphore = {}, std::shared_ptr<VKWrapper::VKFence> signalFence = nullptr);
		void SubmitNowAndWait(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, const CmdSyncSeamphore& syncSeamphore = {});

		template<typename TextureType>
		void PushTexWithLayout(const std::shared_ptr<TextureType>& tex, const RenderGraphResourceLayout& layout) {
			if (tex)
				_texDatas.push_back(TexLayoutData{ tex->GetImageSharedPtr(), layout });
		}

	public:
		void Start();
		void End();

		// 渲染性能统计：串行执行时，等前面的 Pass 在 GPU 上执行完、轮到自己提交的累计时间（不统计时为空）
		void SetExecuteWaitRecord(int64_t* waitUs) { _executeWaitUs = waitUs; }

	private:
		void StartupCmd();
		void Need();
		void LockForExecute();

	private:
		struct PassFrameSubmitCMDData
		{
			std::shared_ptr<VKWrapper::VKCommandBuffer> cmd;
			CmdSyncSeamphore syncSeamphore;
			std::shared_ptr<VKWrapper::VKFence> signalFence;
		};

	private:
		bool _isEnd;
		bool _immediatelySubmit;
		bool _started = false;				// 本 Pass 的补差 barrier 是否已提交
		uint64_t _lastSignaled = 0;			// 本 Pass 最后一次提交在链上 signal 的值（SubmitNowAndWait 等它）
		std::queue<PassFrameSubmitCMDData> _submitcmds;
		std::shared_ptr<GraphSubmitContext> _submitCtx;
		std::vector<TexLayoutData> _texDatas;
		int64_t* _executeWaitUs = nullptr;
	};
}

namespace std {
	template<> struct hash<RenderGraph::TextureHandle> {
		size_t operator()(const RenderGraph::TextureHandle& handle) const {
			return hash<uint32_t>()(handle.GetID());
		}
	};
}

namespace std {
	template<> struct hash<RenderGraph::RenderGraphResource> {
		size_t operator()(const RenderGraph::RenderGraphResource& res) const {
			size_t h1 = std::hash<std::string>{}(res.name);
			size_t h2 = std::hash<int>{}(static_cast<int>(res.type));
			return h1 ^ (h2 << 1);
		}
	};
}

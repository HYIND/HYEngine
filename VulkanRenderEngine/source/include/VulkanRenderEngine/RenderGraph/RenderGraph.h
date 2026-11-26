#pragma once

#include "vkstdafx.h"
#include "RenderGraphContext.h"
#include "RenderGraphResourceManager.h"
#include "PassNode.h"
#include "DependencySolver.h"
#include "ThreadPool.h"
#include "VulkanRenderEngine/General/RenderProfiler.h"

namespace RenderGraph
{

	class Graph
	{
	public:
		Graph(const std::string& name = "");
		~Graph();

		RenderGraphResource CreateTexture(const TextureDesc& desc, const ResourceName& name);					// 获取资源声明
		ExternalResource CreateExternalTexture(const ResourceName& name);										// 获取外部资源声明

		PassNode* AddNode(const std::string& name);		// 添加Node
		PassNode* AddFence(const std::string& name);	// 添加栅栏

		void Compile();									// 编译（分析依赖和生命周期）
		void Execute(
			std::shared_ptr<RenderState>& state,
			const std::unordered_map<std::string, std::shared_ptr<Texture2D>>& externalResources = {}
		);		// 执行

		void Clear();// 清空

		PassNode* GetPass(const std::string& name) const;
		void SetPassEnabled(const std::string& name, bool enabled);// 配置

		void CleanGraphIdleResource();

		// 按新尺寸重建渲染图后调用：本图各 Pass 声明的 Persistent / PersistentOption 资源，
		// 旧图里有同名且描述一致的（尺寸无关的资源，如 DDGI 图集）连同内容接手过来；尺寸变了的不接手，用新的。
		// 返回接手的数量
		uint32_t InheritPersistentResources(Graph& old);

		// 编译后的结构（节点名 / 依赖 / batch），给渲染性能面板画节点图
		std::shared_ptr<const RenderGraphProfileLayout> GetProfileLayout() const { return _profileLayout; }


	private:

		struct PassExecuteContext
		{
			PassNode* node = nullptr;
			FrameDataRegistry registry;
			bool enable;
			std::atomic<uint32_t> dependency;
			std::vector<uint32_t> nextIndexs;
			PassExecuteContext();
			PassExecuteContext(PassExecuteContext&& other) noexcept;
		};

		struct BatchData
		{
			struct PassData
			{
				int passIndex;
				std::shared_ptr<ThreadPool::SubmitHandle<void>> executeHandle;
			};

			bool isEnd = false;
			int batchIndex = -1;
			std::vector<PassData> passes;
		};

		bool GetBatch(int& startIndex, std::vector<BatchData::PassData>& passes, int& batchIndex);
		void FindReadyNodeAndExcute(
			std::vector<std::shared_ptr<ThreadPool::SubmitHandle<void>>>& BeginHandles,
			BatchData& batchdata, std::vector<PassExecuteContext>& passCtxs,
			RenderState& state,
			const std::string& resPrefix,
			FrameLocalResourceRecord& frameLocalResRecord,
			ExternalResourceManager& externalResManager,
			std::atomic<uint32_t>& doneCounter,
			std::atomic<uint32_t>* transientRefs,
			std::shared_ptr<GraphSubmitContext>& submitCtx
		);
		void BuildProfileLayout();
		void BuildTransientRefCounts();

		// 返回是否真的执行了（ShouldExecute 为 false 时返回 false）
		bool ExecutePass(
			PassNode* node,
			FrameDataRegistry& registry,
			RenderState& state,
			const std::string& resPrefix,
			FrameLocalResourceRecord& frameLocalResRecord,
			ExternalResourceManager& externalResManager,
			std::shared_ptr<GraphSubmitContext>& submitCtx,
			int64_t* executeWaitUs = nullptr,		// 渲染性能统计：记「等待提交」
			uint32_t gpuQuery = UINT32_MAX			// 渲染性能统计：本 Pass 的 GPU 时间戳查询下标（UINT32_MAX = 不记）
		);

	private:
		std::string _name;

		ResourceManager _resManager;

		std::vector<PassNode*> _passes;
		std::vector<std::unique_ptr<PassNode>> _ownedPasses;
		std::vector<PassNode*> _sortedPasses;

		std::unordered_map<RenderGraphResource, ResourceUsage> _lifecycles;

		// 临时资源（Input / InputOption / Output / Temp）按 Pass 粒度回收：
		// 编译时给每个资源编号、统计用到它的 Pass 数（同一 Pass 多次声明只算一次）；
		// 每帧拷一份计数，用到它的 Pass 执行完（含 ShouldExecute 为 false、被禁用）就减一，减到 0 立即还给纹理池。
		// Persistent / PersistentOption / External / FrameLocal 不参与
		std::vector<RenderGraphResource> _transientResources;		// 编号 -> 资源
		std::vector<uint32_t> _transientInitRefs;					// 编号 -> 使用它的 Pass 数
		std::vector<std::vector<uint32_t>> _passTransientIds;		// 排序后的 Pass 下标 -> 它用到的资源编号

		bool _needsCompile = true;
		int _compiledVersion = 0;
		std::shared_ptr<const RenderGraphProfileLayout> _profileLayout;

		ThreadPool _executeParallelPool;
		ThreadPool _frameParallelPool;

		int64_t _lastCleanupTimeAccumulator;
		int64_t _CleanupThresold = 10;
	};

}
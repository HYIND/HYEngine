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
		struct FilghtSync
		{
			std::shared_ptr<VKWrapper::VKTimelineSemaphore> preFrameTimeLine;
			std::shared_ptr<VKWrapper::VKTimelineSemaphore> curframeTimeLine;
			uint32_t curProgress = 0;
			int64_t* waitAccumUs = nullptr;		// 渲染性能统计：等上一帧放行的累计耗时（不统计时为空）

			void WaitForNextProgress();
			void SignalDoneProgress();
			//void WaitUntil(uint32_t targetProgress) { preFrameTimeLine->Wait(targetProgress); }

			// 一段工作的作用域：构造时等上一帧放行，析构时放行下一帧。用法见 RenderGraph::FlightSyncScope
			struct ProgressScope
			{
				FilghtSync& sync;
				explicit ProgressScope(FilghtSync& sync) :sync(sync)
				{
					if (sync.waitAccumUs)
					{
						const int64_t start = RenderProfileNowUs();
						sync.WaitForNextProgress();
						*sync.waitAccumUs += RenderProfileNowUs() - start;
					}
					else
						sync.WaitForNextProgress();
				}
				~ProgressScope() { sync.SignalDoneProgress(); }

				ProgressScope(const ProgressScope&) = delete;
				ProgressScope& operator=(const ProgressScope&) = delete;
			};
		};

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
			std::shared_ptr<FilghtSync>& sync,
			const std::unordered_map<std::string, std::shared_ptr<Texture2D>>& externalResources = {}
		);		// 执行

		void Clear();// 清空

		PassNode* GetPass(const std::string& name) const;
		void SetPassEnabled(const std::string& name, bool enabled);// 配置

		void CleanGraphIdleResource();

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
			std::vector<RenderGraphResource> batchLifeCycleResource;
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
			std::shared_ptr<CriticalSectionLock>& _cmdMutex
		);
		void BuildProfileLayout();

		// 返回是否真的执行了（ShouldExecute 为 false 时返回 false）
		bool ExecutePass(
			PassNode* node,
			FrameDataRegistry& registry,
			RenderState& state,
			const std::string& resPrefix,
			FrameLocalResourceRecord& frameLocalResRecord,
			ExternalResourceManager& externalResManager,
			std::shared_ptr<CriticalSectionLock>& _cmdMutex,
			int64_t* executeWaitUs = nullptr		// 渲染性能统计：记「等待执行」
		);

	private:
		std::string _name;

		ResourceManager _resManager;

		std::vector<PassNode*> _passes;
		std::vector<std::unique_ptr<PassNode>> _ownedPasses;
		std::vector<PassNode*> _sortedPasses;

		std::unordered_map<RenderGraphResource, ResourceUsage> _lifecycles;

		bool _needsCompile = true;
		int _compiledVersion = 0;
		std::shared_ptr<const RenderGraphProfileLayout> _profileLayout;

		ThreadPool _executeParallelPool;
		ThreadPool _frameParallelPool;

		int64_t _lastCleanupTimeAccumulator;
		int64_t _CleanupThresold = 10;
	};

	// 多帧流水线的一段：{ RenderGraph::FlightSyncScope syncScope(*sync); ... }
	using FlightSyncScope = Graph::FilghtSync::ProgressScope;
}
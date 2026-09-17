#pragma once

#include "vkstdafx.h"
#include "RenderGraphContext.h"
#include "RenderGraphResourceManager.h"
#include "PassNode.h"
#include "DependencySolver.h"
#include "ThreadPool.h"

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

			void WaitForNextProgress() {
				if (preFrameTimeLine)
					preFrameTimeLine->Wait(curProgress + 1);
				curProgress++;
			}
			void SignalDoneProgress() {
				curframeTimeLine->Signal(curProgress);
			}
			//void WaitUntil(uint32_t targetProgress) { preFrameTimeLine->Wait(targetProgress); }

			struct FilghtSyncProgressGuard
			{
				FilghtSync& sync;
				FilghtSyncProgressGuard(FilghtSync& sync) :sync(sync) { sync.WaitForNextProgress(); }
				~FilghtSyncProgressGuard() { sync.SignalDoneProgress(); }
			};
			auto MakeProgressGuard() {
				return FilghtSyncProgressGuard(*this);
			}
		};

	public:
		Graph(const std::string& name = "");
		~Graph();

		RenderGraphResource CreateTexture(const TextureDesc& desc, const ResourceName& name);					// 获取资源声明
		ExternalResource CreateExternalTexture(const ResourceName& name);										// 获取外部资源声明

		PassNode* AddPass(const std::string& name);		// 添加Pass
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

		void SetRenderTargetFBO(std::shared_ptr<VKWrapper::VKFrameBuffer> fbo);

		void Run();
		void ExecuteLoop();

	private:

		struct PassExecuteContext
		{
			PassNode* node = nullptr;
			FrameDataRegistry registry;
			bool enable;
			bool isDone = false;
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
			ExternalResourceManager& externalResManager
		);
		void ExcutePass(
			PassNode* node, 
			FrameDataRegistry& registry,
			RenderState& state, 
			const std::string& resPrefix,
			ExternalResourceManager& externalResManager
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

		std::shared_ptr<VKWrapper::VKFrameBuffer> _renderTargetFBO;

		ThreadPool _executeParallelPool;
		ThreadPool _frameParallelPool;

		int64_t _lastCleanupTimeAccumulator;
		int64_t _CleanupThresold = 10;
	};
}
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
		Graph(const std::string& name = "");
		~Graph();


		RenderGraphResource CreateTexture(const TextureDesc& desc, const ResourceName& name);					// 获取资源声明

		ExternalResource CreateExternalTexture(const ResourceName& name);										// 获取外部资源声明	
		ExternalResource InjectExternalTexture(const ResourceName& name, std::shared_ptr<Texture2D> texture);	// 注入外部资源
		std::shared_ptr<Texture2D> GetExternalTexture(const ResourceName& name) const;

		void RemoveExternalTexture(const ResourceName& name);

		PassNode* AddPass(const std::string& name);		// 添加Pass
		PassNode* AddFence(const std::string& name);	// 添加栅栏

		void Compile();							// 编译（分析依赖和生命周期）
		void EarlyExecute(RenderState& state);	// 早期提前执行，该函数应提前于Execute执行
		void Execute(RenderState& state);		// 执行

		void Clear();// 清空

		PassNode* GetPass(const std::string& name) const;
		void SetPassEnabled(const std::string& name, bool enabled);// 配置

		void SetRenderTargetFBO(std::shared_ptr<VKWrapper::VKFrameBuffer> fbo);

	private:
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
		void FindReadyNodeAndExcute(std::vector<std::shared_ptr<ThreadPool::SubmitHandle<void>>>& BeginHandles, BatchData& batchdata, RenderState& state, uint32_t frameIndex);
		void ExcutePass(PassNode* node, RenderState& state, uint32_t frameIndex);
		void EndPass(PassNode* node, BatchData& batchdata);;

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

		ThreadPool _earlyParallelPool;
		ThreadPool _executeParallelPool;
		ThreadPool _frameParallelPool;

		int64_t _lastCleanupTimeAccumulator;
		int64_t _CleanupThresold = 10;

		std::atomic<uint32_t> _earlyFrameIndex{ 0 };
		std::atomic<uint32_t> _executeFrameIndex{ 0 };
		uint32_t _maxFramesInFlight = 1;  // 最大超前帧数
	};
}
#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderGraph/RenderGraph.h"
#include "CriticalSectionLock.h"

using namespace RenderGraph;

Graph::PassExecuteContext::PassExecuteContext() : dependency(0) {}

RenderGraph::Graph::PassExecuteContext::PassExecuteContext(PassExecuteContext&& other) noexcept
	: node(other.node)
	, registry(std::move(other.registry))
	, enable(other.enable)
	, dependency(other.dependency.load(std::memory_order_relaxed))
	, nextIndexs(std::move(other.nextIndexs))
{}

Graph::Graph(const std::string& name)
	:_name(name)
{
	_frameParallelPool.start();
	_executeParallelPool.start();
}

Graph::~Graph()
{
	Clear();
}

// 资源创建
RenderGraphResource Graph::CreateTexture(const TextureDesc& desc, const ResourceName& name)
{
	RenderGraphResource res;
	res.name = name;
	res.type = ResourceType::Texture;
	res.desc = desc;
	return res;
}

ExternalResource Graph::CreateExternalTexture(const ResourceName& name)
{
	ExternalResource res;
	res.name = name;
	res.type = ResourceType::Texture;
	return res;
}

// Pass管理
PassNode* Graph::AddNode(const std::string& name) {
	auto node = std::make_unique<PassNode>(name);
	auto rawptr = node.get();
	_passes.push_back(rawptr);
	_ownedPasses.push_back(std::move(node));
	_needsCompile = true;
	return rawptr;
}

PassNode* Graph::AddFence(const std::string& name)
{
	auto node = std::make_unique<PassNode>(name);
	node->SetEnable(false);
	auto rawptr = node.get();
	_passes.push_back(rawptr);
	_ownedPasses.push_back(std::move(node));
	_needsCompile = true;
	return rawptr;
}

// 编译（分析依赖和生命周期）
void Graph::Compile() {
	if (!_needsCompile) return;

	// 排序
	_sortedPasses = DependencySolver::SortPasses(_passes);
	DependencySolver::PrintPasses(_name, _sortedPasses);

	std::unordered_map<PassNode*, int> nodeToIndex;
	for (int i = 0; i < _sortedPasses.size(); i++)
	{
		auto& node = _sortedPasses[i];
		node->_dependency = 0;
		node->_nextIndexs.clear();
		nodeToIndex[node] = i;
	}

	for (auto pass : _sortedPasses)
	{
		auto it = nodeToIndex.find(pass);
		if (it != nodeToIndex.end())
		{
			for (auto prev : pass->_afters)
			{
				prev->_nextIndexs.push_back(it->second);
				pass->_dependency++;
			}
		}
	}

	// 计算生命周期
	_lifecycles = DependencySolver::CalculateLifetimes(_sortedPasses);
	DependencySolver::PrintLifecycles(_name, _lifecycles);

	_needsCompile = false;
	_compiledVersion++;
}

bool Graph::GetBatch(int& startIndex, std::vector<BatchData::PassData>& passes, int& batchIndex)
{
	if (startIndex >= _sortedPasses.size())
		return false;

	int lastBatch = _sortedPasses[startIndex]->GetBatch();
	while (startIndex < _sortedPasses.size())
	{
		auto* pass = _sortedPasses[startIndex];
		int currentBatch = pass->GetBatch();

		if (currentBatch != lastBatch || lastBatch == -1)
			break;

		passes.push_back(BatchData::PassData{ .passIndex = startIndex });
		startIndex++;
	}

	batchIndex = lastBatch;
	return !passes.empty();
};

void Graph::FindReadyNodeAndExcute(
	std::vector<std::shared_ptr<ThreadPool::SubmitHandle<void>>>& BeginHandles,
	BatchData& batchdata,
	std::vector<PassExecuteContext>& passCtxs,
	RenderState& state,
	const std::string& resPrefix,
	ExternalResourceManager& externalResManager,
	std::atomic<uint32_t>& doneCounter,
	std::shared_ptr<CriticalSectionLock>& _cmdMutex
)
{
	for (auto& passData : batchdata.passes)
	{
		auto& executeHandle = passData.executeHandle;
		if (!executeHandle)
		{
			int idx = passData.passIndex;
			auto& handle = BeginHandles[idx];
			auto& ctx = passCtxs[idx];
			if (handle->is_ready() && ctx.dependency.load() <= 0)
			{
				executeHandle = _executeParallelPool.submit(
					[&, &ctx = passCtxs[idx], &passCtxs = passCtxs]()->void {
						ExecutePass(ctx.node, ctx.registry, state, resPrefix, externalResManager, _cmdMutex);
						for (auto& next : ctx.nextIndexs)
							--passCtxs[next].dependency;
						doneCounter++;
					});
			}
		}
	}

	for (auto it = batchdata.passes.begin(); it != batchdata.passes.end();)
	{
		auto& passData = *it;
		auto& executeHandle = passData.executeHandle;
		if (executeHandle && executeHandle->is_ready())
		{
			int idx = passData.passIndex;
			auto& ctx = passCtxs[idx];
			auto& passLifeTimeResource = ctx.node->GetLifeCycleResource();
			batchdata.batchLifeCycleResource.insert(
				batchdata.batchLifeCycleResource.end(),
				passLifeTimeResource.begin(),
				passLifeTimeResource.end());
			it = batchdata.passes.erase(it);
			continue;
		}
		++it;
	}

	if (batchdata.passes.empty())
		batchdata.isEnd = true;

}

void Graph::ExecutePass(
	PassNode* node,
	FrameDataRegistry& registry,
	RenderState& state,
	const std::string& resPrefix,
	ExternalResourceManager& externalResManager,
	std::shared_ptr<CriticalSectionLock>& _cmdMutex
)
{
	//auto start = Tool::GetTimestampMircoseconds();
	//std::cout << std::format("ExecutePass {}, threadid = {}\n", node->GetName(), std::this_thread::get_id());

	if (node->ShouldExecute(registry, state))
	{
		PassFrameCmdContext cmdCtx(_cmdMutex);
		PassFrameContext ctx;
		ctx.passName = node->GetName();

		for (const auto& input : node->GetInputs()) {
			auto tex = _resManager.GetTexture(input.resource, resPrefix);
			ctx.inputTextures.push_back(tex);
			cmdCtx.PushTexWithLayout(tex, input.layout);
		}

		for (const auto& input : node->GetInputOptions()) {
			auto tex = _resManager.TryGetTexture(input.resource, resPrefix);
			ctx.optionInputTextures.push_back(tex);
			cmdCtx.PushTexWithLayout(tex, input.layout);
		}

		for (const auto& output : node->GetOutputs()) {
			auto tex = _resManager.GetTexture(output.resource, resPrefix);
			ctx.outputTextures.push_back(tex);
			cmdCtx.PushTexWithLayout(tex, output.layout);
		}

		for (const auto& temp : node->GetTemps()) {
			auto tex = _resManager.GetTexture(temp.resource, resPrefix);
			ctx.tempTextures.push_back(tex);
			cmdCtx.PushTexWithLayout(tex, temp.layout);
		}

		for (const auto& persitent : node->GetPersistents()) {
			auto tex = _resManager.GetTexture(persitent.resource, "");
			ctx.persitentTextures.push_back(tex);
			cmdCtx.PushTexWithLayout(tex, persitent.layout);
		}

		for (const auto& external : node->GetExternals()) {
			if (external.resource.type == ResourceType::Texture)
			{
				auto tex = externalResManager.GetExternalTexture(external.resource.name);
				ctx.externalTextures.push_back(tex);
				cmdCtx.PushTexWithLayout(tex, external.layout);
			}
		}

		node->Execute(cmdCtx, registry, ctx, state);
	}

	//std::cout << std::format("ExecutePass {} ,cost {}ms\n", pass->GetName(), Tool::GetTimestampMircoseconds() - start);
}


// 执行
void Graph::Execute(
	std::shared_ptr<RenderState>& state,
	std::shared_ptr<FilghtSync>& sync,
	const std::unordered_map<std::string, std::shared_ptr<Texture2D>>& externalResources
)
{
	if (!state)
		return;

	auto _cmdMutex = std::make_shared<CriticalSectionLock>();
	std::atomic<uint32_t> doneCounter(0u);
	std::atomic<uint32_t> beginCounter(0u);

	std::vector<PassExecuteContext> passExeContext;
	passExeContext.reserve(_sortedPasses.size());
	for (auto node : _sortedPasses)
	{
		passExeContext.emplace_back();
		auto& ctx = passExeContext.back();
		ctx.node = node;
		ctx.enable = node->GetEnable();
		ctx.dependency.store(
			node->_dependency,
			std::memory_order_relaxed);
		ctx.nextIndexs = node->_nextIndexs;
	}

	std::vector<std::shared_ptr<ThreadPool::SubmitHandle<void>>> BeginHandles;
	BeginHandles.reserve(passExeContext.size());
	for (auto& passCtx : passExeContext)
	{
		BeginHandles.push_back(std::move(_frameParallelPool.submit(
			[&beginCounter, node = passCtx.node, registryPtr = &passCtx.registry, &state]()->void
			{
				node->FrameBegin(*registryPtr, *state);
				beginCounter++;
			})
		));
	}


	auto resPrefix = Tool::GenerateSimpleUuid();
	ExternalResourceManager externalResManager(externalResources);

	std::vector<BatchData> running_batchs;
	running_batchs.reserve(_sortedPasses.size());
	std::map<int, std::vector<BatchData>, std::less<int>> end_batchs_map;

	int passIndex = 0;
	int batchIndex = -1;
	std::vector<BatchData::PassData> batchpasses;
	while (GetBatch(passIndex, batchpasses, batchIndex))
	{
		running_batchs.push_back(BatchData{ .batchIndex = batchIndex, .passes = std::move(batchpasses) });
		batchpasses.clear();
	}

	int endPassClearIndex = -1;

	auto ClearBatch = [&]()
		{
			for (auto it = end_batchs_map.begin(); it != end_batchs_map.end();)
			{
				int index = it->first;
				auto& batchs = it->second;
				if (index > endPassClearIndex)
				{
					it++;
					continue;
				}
				else
				{
					for (auto& batch : batchs)
					{
						for (auto& res : batch.batchLifeCycleResource)
						{
							auto it = _lifecycles.find(res);
							if (it == _lifecycles.end())
								continue;

							auto& lifecycle = it->second;
							if (lifecycle.lastBatch <= batch.batchIndex)
							{
								_resManager.ReleaseTexture(res, resPrefix);
								//std::cout << std::format("release res [{}]\n", res.name);
							}
						}
					}
					it = end_batchs_map.erase(it);
				}
			}
		};

	auto OnBatchEnd = [&](BatchData&& endbatch)
		{
			end_batchs_map[endbatch.batchIndex].push_back(std::move(endbatch));
			for (auto& it : end_batchs_map)
			{
				int index = it.first;
				if (index == (endPassClearIndex + 1))
				{
					endPassClearIndex = index;
					//sync->SignalDoneProgress();
				}
			}
			ClearBatch();
		};

	uint32_t targetDoneCounter = passExeContext.size();
	uint32_t lastBeginCounter = std::numeric_limits<uint32_t>::max();
	uint32_t lastDoneCounter = std::numeric_limits<uint32_t>::max();
	while (!running_batchs.empty())
	{
		//sync->WaitForNextProgress();
		auto curBeginCounter = beginCounter.load(std::memory_order_acquire);
		auto curDoneCounter = doneCounter.load(std::memory_order_acquire);
		if (
			(curDoneCounter == lastDoneCounter && curDoneCounter < targetDoneCounter)
			&& (curBeginCounter == lastBeginCounter && curBeginCounter < targetDoneCounter)
			)
		{
			std::this_thread::yield();
			continue;
		}

		lastDoneCounter = curDoneCounter;
		lastBeginCounter = curBeginCounter;

		for (auto it = running_batchs.begin(); it != running_batchs.end(); )
		{
			auto& batch = *it;
			if (!batch.isEnd)
				FindReadyNodeAndExcute(BeginHandles, batch, passExeContext, *state, resPrefix, externalResManager, doneCounter, _cmdMutex);

			if (batch.isEnd)
			{
				OnBatchEnd(std::move(batch));
				it = running_batchs.erase(it);
			}
			else
				it++;
		}

	}

	if (!end_batchs_map.empty())
	{
		endPassClearIndex = INT_MAX;
		ClearBatch();
	}

	//std::cout << "===========================\n";

	std::vector<std::shared_ptr<ThreadPool::SubmitHandle<void>>> EndHandles;
	EndHandles.reserve(passExeContext.size());
	for (auto& passCtx : passExeContext)
	{
		EndHandles.push_back(std::move(_frameParallelPool.submit(
			[node = passCtx.node, registryPtr = &passCtx.registry, &state]()->void
			{
				node->FrameEnd(*registryPtr, *state);
			})
		));
	}
	for (auto& handle : EndHandles)
		handle->get();

	//std::cout << std::format("Execute done {}\n", frameIndex);
}

// 清空
void Graph::Clear() {
	_passes.clear();
	_sortedPasses.clear();
	_ownedPasses.clear();
	_lifecycles.clear();
	_needsCompile = true;
	_compiledVersion++;
}

PassNode* Graph::GetPass(const std::string& name) const {
	for (auto* pass : _passes) {
		if (pass->GetName() == name) {
			return pass;
		}
	}
	return nullptr;
}

// 配置
void Graph::SetPassEnabled(const std::string& name, bool enabled) {
	for (auto* pass : _passes) {
		if (pass->GetName() == name) {
			pass->SetEnable(enabled);
			return;
		}
	}
}

void Graph::SetRenderTargetFBO(std::shared_ptr<VKWrapper::VKFrameBuffer> fbo)
{
	_renderTargetFBO = fbo;
}


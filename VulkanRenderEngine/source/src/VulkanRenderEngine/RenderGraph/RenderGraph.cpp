#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderGraph/RenderGraph.h"

using namespace RenderGraph;

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
PassNode* Graph::AddPass(const std::string& name) {
	auto pass = std::make_unique<PassNode>(name);
	_passes.push_back(pass.get());
	_ownedPasses.push_back(std::move(pass));
	_needsCompile = true;
	return _passes.back();
}

PassNode* Graph::AddFence(const std::string& name)
{
	auto pass = std::make_unique<PassNode>(name);
	pass->SetEnable(false);
	_passes.push_back(pass.get());
	_ownedPasses.push_back(std::move(pass));
	_needsCompile = true;
	return _passes.back();
}

// 编译（分析依赖和生命周期）
void Graph::Compile() {
	if (!_needsCompile) return;

	// 排序
	_sortedPasses = DependencySolver::SortPasses(_passes);
	DependencySolver::PrintPasses(_name, _sortedPasses);

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
	ExternalResourceManager& externalResManager
)
{

	static auto canExcute = [](PassNode* node, std::vector<PassExecuteContext>& passCtxs)-> bool {
		for (auto& node : node->GetAfters())
		{
			for (auto& ctx : passCtxs)
			{
				if (ctx.node != node)
					continue;
				if (!ctx.isDone)
					return false;
			}
		}
		return true;
		};

	for (auto it = batchdata.passes.begin(); it != batchdata.passes.end(); )
	{
		auto& passData = *it;
		int idx = passData.passIndex;
		auto& executeHandle = passData.executeHandle;

		auto& handle = BeginHandles[idx];
		if (!executeHandle)
		{
			auto* node = passCtxs[idx].node;
			if (handle->is_ready() && canExcute(node, passCtxs))
			{
				executeHandle = _executeParallelPool.submit(
					[&, idx = idx, node = node]()->void {
						ExcutePass(node, passCtxs[idx].registry, state, resPrefix, externalResManager);
					});
				executeHandle.get();
			}
		}
		else
		{
			if (executeHandle->is_ready())
			{
				passCtxs[idx].isDone = true;
				auto* node = passCtxs[idx].node;
				auto& passLifeTimeResource = node->GetLifeCycleResource();
				batchdata.batchLifeCycleResource.insert(
					batchdata.batchLifeCycleResource.end(),
					passLifeTimeResource.begin(),
					passLifeTimeResource.end());
				it = batchdata.passes.erase(it);
				continue;
			}
		}
		++it;
	}

	if (batchdata.passes.empty())
		batchdata.isEnd = true;
}

void Graph::ExcutePass(
	PassNode* node,
	FrameDataRegistry& registry,
	RenderState& state,
	const std::string& resPrefix,
	ExternalResourceManager& externalResManager
)
{
	//auto start = Tool::GetTimestampMircoseconds();
	//std::cout << std::format("ExcutePass {}\n", pass->GetName());

	if (node->ShouldExecute(registry, state))
	{
		PassFrameContext ctx;
		ctx.passName = node->GetName();

		for (const auto& input : node->GetInputs()) {
			ctx.inputTextures.push_back(_resManager.GetTexture(input, resPrefix));
		}

		for (const auto& input : node->GetInputOptions()) {
			ctx.optionInputTextures.push_back(_resManager.TryGetTexture(input, resPrefix));
		}

		for (const auto& output : node->GetOutputs()) {
			ctx.outputTextures.push_back(_resManager.GetTexture(output, resPrefix));
		}

		for (const auto& temp : node->GetTemps()) {
			ctx.tempTextures.push_back(_resManager.GetTexture(temp, resPrefix));
		}

		for (const auto& persitent : node->GetPersistents()) {
			ctx.persitentTextures.push_back(_resManager.GetTexture(persitent, ""));
		}

		for (const auto& external : node->GetExternals()) {
			if (external.type == ResourceType::Texture)
				ctx.externalTextures.push_back(externalResManager.GetExternalTexture(external.name));
		}

		node->Execute(registry, ctx, state);
		//std::cout << std::format("ExcutePass {}\n ", pass->GetName());
	}

	//std::cout << std::format("ExcutePass {} ,cost {}ms\n", pass->GetName(), Tool::GetTimestampMircoseconds() - start);
}
;

// 执行
void Graph::Execute(
	std::shared_ptr<RenderState>& state,
	std::shared_ptr<FilghtSync>& sync,
	const std::unordered_map<std::string, std::shared_ptr<Texture2D>>& externalResources
)
{
	if (!state)
		return;

	std::vector<PassExecuteContext> passExeContext;
	passExeContext.resize(_sortedPasses.size());
	for (size_t i = 0; i < _sortedPasses.size(); i++)
	{
		passExeContext[i].node = _sortedPasses[i];
		passExeContext[i].enable = _sortedPasses[i]->GetEnable();
	}

	std::vector<std::shared_ptr<ThreadPool::SubmitHandle<void>>> BeginHandles;
	BeginHandles.reserve(passExeContext.size());
	for (auto& passCtx : passExeContext)
	{
		BeginHandles.push_back(std::move(_frameParallelPool.submit(
			[node = passCtx.node, registryPtr = &passCtx.registry, &state]()->void
			{
				node->FrameBegin(*registryPtr, *state);
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

	while (!running_batchs.empty())
	{
		//sync->WaitForNextProgress();
		for (auto it = running_batchs.begin(); it != running_batchs.end(); )
		{
			auto& batch = *it;
			if (!batch.isEnd)
				FindReadyNodeAndExcute(BeginHandles, batch, passExeContext, *state, resPrefix, externalResManager);

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
		BeginHandles.push_back(std::move(_frameParallelPool.submit(
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

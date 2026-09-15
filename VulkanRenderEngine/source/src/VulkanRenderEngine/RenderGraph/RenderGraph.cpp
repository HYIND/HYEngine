#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderGraph/RenderGraph.h"

using namespace RenderGraph;

Graph::Graph(const std::string& name)
	:_name(name)
{
	_frameParallelPool.start();
	_executeParallelPool.start();
	_earlyParallelPool.start();
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

// 注入外部资源
ExternalResource Graph::InjectExternalTexture(const ResourceName& name, std::shared_ptr<Texture2D> texture)
{
	_resManager.RegisterExternalTexture(name, texture);
	_needsCompile = true;
	return CreateExternalTexture(name);
}

void Graph::RemoveExternalTexture(const ResourceName& name) {
	_resManager.UnregisterExternalTexture(name);
	_needsCompile = true;
}

std::shared_ptr<Texture2D> Graph::GetExternalTexture(const ResourceName& name) const {
	return _resManager.GetExternalTexture(name);
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

void Graph::EarlyExecute(RenderState& state)
{
	if (_needsCompile)
		Compile();

	uint32_t executeIdx = _executeFrameIndex.load(std::memory_order_acquire);
	uint32_t earlyIdx = _earlyFrameIndex.load(std::memory_order_acquire);

	while (earlyIdx - executeIdx > _maxFramesInFlight)
	{
		std::this_thread::yield();
		executeIdx = _executeFrameIndex.load(std::memory_order_acquire);
		earlyIdx = _earlyFrameIndex.load(std::memory_order_acquire);
	}

	uint32_t frameIndex = _earlyFrameIndex.fetch_add(1);

	std::vector<std::shared_ptr<ThreadPool::SubmitHandle<void>>> EarlyHandles;
	for (auto& pass : _sortedPasses)
	{
		EarlyHandles.push_back(std::move(_earlyParallelPool.submit(
			[pass = pass, &state, &frameIndex]()->void
			{
				pass->EarlyExecute(frameIndex, state);
			})
		));
	}

	for (auto& handle : EarlyHandles)
		handle->get();

	//std::cout << std::format("Early done {}\n", frameIndex);
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

void Graph::FindReadyNodeAndExcute(std::vector<std::shared_ptr<ThreadPool::SubmitHandle<void>>>& BeginHandles, BatchData& batchdata, RenderState& state, uint32_t frameIndex)
{

	static auto canExcute = [](PassNode* node)-> bool {
		for (auto& pass : node->GetAfters())
		{
			if (!pass->IsDone())
				return false;
		}
		return true;
		};

	//for (auto it = batchdata.passes.begin(); it != batchdata.passes.end(); )
	//{
	//	int idx = *it;
	//	auto& handle = BeginHandles[idx];
	//	auto* node = _sortedPasses[idx];
	//	if (handle->is_ready() && canExcute(_sortedPasses[idx]))
	//	{
	//		ExcutePass(node, state, frameIndex);
	//		EndPass(node, batchdata);
	//		it = batchdata.passes.erase(it);
	//		continue;
	//	}
	//	++it;
	//}

	for (auto it = batchdata.passes.begin(); it != batchdata.passes.end(); )
	{
		auto& passData = *it;
		int idx = passData.passIndex;
		auto& executeHandle = passData.executeHandle;

		auto& handle = BeginHandles[idx];
		if (!executeHandle)
		{
			if (handle->is_ready() && canExcute(_sortedPasses[idx]))
			{
				auto* node = _sortedPasses[idx];
				executeHandle = _executeParallelPool.submit([&, frameIndex = frameIndex, node = node]()->void { ExcutePass(node, state, frameIndex); });
				//executeHandle = _executeParallelPool.submit_to(0, [&, frameIndex = frameIndex, node = node]()->void { ExcutePass(node, state, frameIndex); });
			}
		}
		else
		{
			if (executeHandle->is_ready())
			{
				auto* node = _sortedPasses[idx];
				EndPass(node, batchdata);
				it = batchdata.passes.erase(it);
				continue;
			}
		}
		++it;
	}

	if (batchdata.passes.empty())
		batchdata.isEnd = true;
}

void RenderGraph::Graph::EndPass(PassNode* node, BatchData& batchdata) {
	node->SetDone(true);
	auto& passLifeTimeResource = node->GetLifeCycleResource();
	batchdata.batchLifeCycleResource.insert(
		batchdata.batchLifeCycleResource.end(),
		passLifeTimeResource.begin(),
		passLifeTimeResource.end());
}

void Graph::ExcutePass(PassNode* node, RenderState& state, uint32_t frameIndex)
{
	//auto start = Tool::GetTimestampMircoseconds();
	//std::cout << std::format("ExcutePass {}\n", pass->GetName());

	if (node->ShouldExecute(frameIndex, state))
	{
		PassContext ctx;
		ctx.passName = node->GetName();

		for (const auto& input : node->GetInputs()) {
			ctx.inputTextures.push_back(_resManager.GetTexture(input));
		}

		for (const auto& input : node->GetInputOptions()) {
			ctx.optionInputTextures.push_back(_resManager.TryGetTexture(input));
		}

		for (const auto& output : node->GetOutputs()) {
			ctx.outputTextures.push_back(_resManager.GetTexture(output));
		}

		for (const auto& temp : node->GetTemps()) {
			ctx.tempTextures.push_back(_resManager.GetTexture(temp));
		}

		for (const auto& persitent : node->GetPersistents()) {
			ctx.persitentTextures.push_back(_resManager.GetTexture(persitent));
		}

		for (const auto& external : node->GetExternals()) {
			if (external.type == ResourceType::Texture)
				ctx.externalTextures.push_back(_resManager.GetExternalTexture(external.name));
		}

		node->Execute(frameIndex, ctx, state);
		//std::cout << std::format("ExcutePass {}\n ", pass->GetName());
	}

	//std::cout << std::format("ExcutePass {} ,cost {}ms\n", pass->GetName(), Tool::GetTimestampMircoseconds() - start);
}
;

// 执行
void Graph::Execute(RenderState& state)
{
	auto time = Tool::GetTimestampSecond();
	if (time - _lastCleanupTimeAccumulator > _CleanupThresold)
	{
		if (_lastCleanupTimeAccumulator != 0)
			_resManager.CleanupIdleResource();
		_lastCleanupTimeAccumulator = Tool::GetTimestampSecond();
	}

	if (_needsCompile)
		Compile();

	uint32_t frameIndex = _executeFrameIndex.load();

	std::vector<std::shared_ptr<ThreadPool::SubmitHandle<void>>> BeginHandles;
	BeginHandles.reserve(_sortedPasses.size());
	for (auto& pass : _sortedPasses)
	{
		pass->SetDone(false);
		BeginHandles.push_back(std::move(_frameParallelPool.submit(
			[pass = pass, &state, &frameIndex]()->void
			{
				//auto guard = THREADCONTEXT->GetBindGuard();
				//GPUTimer timer;
				pass->FrameBegin(frameIndex, state);
				//std::cout << std::format("Excute FrameBegin {} ,cost {}ms\n", pass->GetName(), timer.End());
			})
		));
	}

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
								_resManager.ReleaseTexture(res);
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
					endPassClearIndex = index;
			}
			ClearBatch();
		};

	while (!running_batchs.empty())
	{
		for (auto it = running_batchs.begin(); it != running_batchs.end(); )
		{
			auto& batch = *it;
			if (!batch.isEnd)
				FindReadyNodeAndExcute(BeginHandles, batch, state, frameIndex);

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
	EndHandles.reserve(_sortedPasses.size());
	for (auto& pass : _sortedPasses)
		EndHandles.push_back(std::move(_frameParallelPool.submit([pass = pass, &state, &frameIndex]()->void {pass->FrameEnd(frameIndex, state); })));
	for (auto& handle : EndHandles)
		handle->get();

	//std::cout << std::format("Execute done {}\n", frameIndex);
	_executeFrameIndex.fetch_add(1);
}

// 清空
void Graph::Clear() {
	_passes.clear();
	_ownedPasses.clear();
	_sortedPasses.clear();
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

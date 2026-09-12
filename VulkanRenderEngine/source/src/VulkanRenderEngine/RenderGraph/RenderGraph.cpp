#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderGraph/RenderGraph.h"

using namespace RenderGraph;

Graph::Graph(const std::string& name)
	:_name(name)
{
	_frameParallelPool.start();
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

	int executeIdx = _executeFrameIndex.load(std::memory_order_acquire);
	int earlyIdx = _earlyFrameIndex.load(std::memory_order_acquire);

	while (earlyIdx - executeIdx > _maxFramesInFlight)
	{
		std::this_thread::yield();
		executeIdx = _executeFrameIndex.load(std::memory_order_acquire);
		earlyIdx = _earlyFrameIndex.load(std::memory_order_acquire);
	}

	int frameIndex = _earlyFrameIndex.fetch_add(1);

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

	int frameIndex = _executeFrameIndex.load();

	struct BatchData
	{
		bool isEnd = false;
		int batchIndex = -1;
		std::vector<int> passes;
		std::vector<RenderGraphResource> batchLifeCycleResource;
	};

	std::vector<std::shared_ptr<ThreadPool::SubmitHandle<void>>> BeginHandles;
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

	auto GetBatch = [&](int& startIndex, std::vector<int>& batchs, int& batchIndex)-> bool
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

				batchs.push_back(startIndex);
				startIndex++;
			}

			batchIndex = lastBatch;
			return !batchs.empty();
		};

	auto FindReadyNodeAndExcute = [&](BatchData& batchdata)-> void
		{
			auto ExcutePass = [&](int passIndex)-> void
				{
					auto* pass = _sortedPasses[passIndex];

					//auto start = Tool::GetTimestampMircoseconds();
					//std::cout << std::format("ExcutePass {}\n", pass->GetName());

					if (pass->ShouldExecute(frameIndex, state))
					{
						PassContext ctx;
						ctx.passName = pass->GetName();

						for (const auto& input : pass->GetInputs()) {
							ctx.inputTextures.push_back(_resManager.GetTexture(input));
						}

						for (const auto& input : pass->GetInputOptions()) {
							ctx.optionInputTextures.push_back(_resManager.TryGetTexture(input));
						}

						for (const auto& output : pass->GetOutputs()) {
							ctx.outputTextures.push_back(_resManager.GetTexture(output));
						}

						for (const auto& temp : pass->GetTemps()) {
							ctx.tempTextures.push_back(_resManager.GetTexture(temp));
						}

						for (const auto& persitent : pass->GetPersistents()) {
							ctx.persitentTextures.push_back(_resManager.GetTexture(persitent));
						}

						for (const auto& external : pass->GetExternals()) {
							if (external.type == ResourceType::Texture)
								ctx.externalTextures.push_back(_resManager.GetExternalTexture(external.name));
						}

						pass->Execute(frameIndex, ctx, state);
					}

					//std::cout << std::format("ExcutePass {} ,cost {}ms\n", pass->GetName(), Tool::GetTimestampMircoseconds() - start);

					pass->SetDone(true);
					auto& passLifeTimeResource = pass->GetLifeCycleResource();
					batchdata.batchLifeCycleResource.insert(
						batchdata.batchLifeCycleResource.end(),
						passLifeTimeResource.begin(),
						passLifeTimeResource.end());
				};

			static auto canExcute = [](PassNode* node)-> bool {
				for (auto& pass : node->GetAfters())
				{
					if (!pass->IsDone())
						return false;
				}
				return true;
				};

			for (auto it = batchdata.passes.begin(); it != batchdata.passes.end(); )
			{
				int idx = *it;
				auto& handle = BeginHandles[idx];

				if (handle->is_ready() && canExcute(_sortedPasses[idx]))
				{
					ExcutePass(idx);
					it = batchdata.passes.erase(it);
				}
				else {
					++it;
				}
			}

			if (batchdata.passes.empty())
				batchdata.isEnd = true;
		};

	std::vector<BatchData> running_batchs;
	std::map<int, std::vector<BatchData>, std::less<int>> end_batchs_map;

	int passIndex = 0;
	int batchIndex = -1;
	std::vector<int>batchpasses;
	while (GetBatch(passIndex, batchpasses, batchIndex))
	{
		running_batchs.push_back(BatchData{ .batchIndex = batchIndex, .passes = batchpasses });
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
				FindReadyNodeAndExcute(batch);

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

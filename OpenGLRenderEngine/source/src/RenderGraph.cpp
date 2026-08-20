#include "OpenGLRenderEngine/RenderGraph/RenderGraph.h"
#include "Helper/Tools.h"
//#include "OpenGLRenderEngine/General/GPUTimer.h"

using namespace OpenGLRenderGraph;

RenderGraph::RenderGraph(const std::string& name)
	:_name(name)
{
	_frameParallelPool.start();
	_earlyParallelPool.start();

	// 为线程池初始化共享上下文
	RENDERCONTEXMANAGER->WithTempReleaseMainOpenGLBind([&]()->void {
		THREADCONTEXT->UnBind();
		std::vector<std::shared_ptr<ThreadPool::SubmitHandle<void>>> Handles;
		for (int i = 0; i < _frameParallelPool.workersize(); i++)
		{
			Handles.push_back(std::move(_frameParallelPool.submit([&]()->void {
				auto guard = THREADCONTEXT->GetBindGuard();
				})
			));
		}
		for (int i = 0; i < _earlyParallelPool.workersize(); i++)
		{
			Handles.push_back(std::move(_earlyParallelPool.submit([&]()->void {
				auto guard = THREADCONTEXT->GetBindGuard();
				})
			));
		}
		for (auto& handle : Handles)
			handle->get();

		THREADCONTEXT->Bind();
		});
}

RenderGraph::~RenderGraph()
{
	Clear();
}

// 资源创建
RenderGraphResource RenderGraph::CreateTexture(const TextureDesc& desc, const ResourceName& name)
{
	RenderGraphResource res;
	res.name = name;
	res.type = ResourceType::Texture;
	res.desc = desc;
	return res;
}

ExternalResource OpenGLRenderGraph::RenderGraph::CreateExternalTexture(const ResourceName& name)
{
	ExternalResource res;
	res.name = name;
	res.type = ResourceType::Texture;
	return res;
}

// 注入外部资源
void RenderGraph::InjectExternalTexture(const ResourceName& name, std::shared_ptr<Texture2D> texture)
{
	_resManager.RegisterExternalTexture(name, texture);

	_needsCompile = true;
}

void RenderGraph::RemoveExternalTexture(const ResourceName& name) {
	_resManager.UnregisterExternalTexture(name);
	_needsCompile = true;
}

std::shared_ptr<Texture2D> RenderGraph::GetExternalTexture(const ResourceName& name) const {
	return _resManager.GetExternalTexture(name);
}

// Pass管理
PassNode* RenderGraph::AddPass(const std::string& name) {
	auto pass = std::make_unique<PassNode>(name);
	_passes.push_back(pass.get());
	_ownedPasses.push_back(std::move(pass));
	_needsCompile = true;
	return _passes.back();
}

PassNode* OpenGLRenderGraph::RenderGraph::AddFence(const std::string& name)
{
	auto pass = std::make_unique<PassNode>(name);
	pass->SetEnable(false);
	_passes.push_back(pass.get());
	_ownedPasses.push_back(std::move(pass));
	_needsCompile = true;
	return _passes.back();
}

// 编译（分析依赖和生命周期）
void RenderGraph::Compile() {
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

void OpenGLRenderGraph::RenderGraph::EarlyExecute(RenderState& state)
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
void RenderGraph::Execute(RenderState& state)
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

	auto FindReadyNodeAndExcute = [&](BatchData& batchdata)-> bool
		{
			auto ExcutePass = [&](int passIndex)-> void
				{
					auto* pass = _sortedPasses[passIndex];

					//std::cout << std::format("ExcutePass {}\n", pass->GetName());
					//GPUTimer timer;

					if (pass->ShouldExecute(frameIndex, state))
					{
						PassContext ctx;
						ctx.renderTargetFBO = _renderTargetFBO;
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

					//std::cout << std::format("ExcutePass {} ,cost {}ms\n", pass->GetName(), timer.End());


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
					it = batchdata.passes.erase(it);
					ExcutePass(idx);
				}
				else {
					++it;
				}
			}

			if (batchdata.passes.empty() && !batchdata.batchLifeCycleResource.empty())
			{
				for (auto& res : batchdata.batchLifeCycleResource)
				{
					auto it = _lifecycles.find(res);
					if (it == _lifecycles.end())
						continue;

					auto& lifecycle = it->second;
					if (lifecycle.lastBatch <= batchdata.batchIndex)
					{
						_resManager.ReleaseTexture(res);
						//std::cout << std::format("release res [{}]\n", res.name);
					}
				}
			}

			return batchdata.passes.empty();
		};

	std::vector<BatchData> all_batchs;

	int passIndex = 0;
	int batchIndex = -1;
	std::vector<int>batchs;
	while (GetBatch(passIndex, batchs, batchIndex))
	{
		all_batchs.push_back({ batchIndex,batchs });
		batchs.clear();
	}

	while (!all_batchs.empty())
	{
		for (auto it = all_batchs.begin(); it != all_batchs.end(); )
		{
			auto& batch = *it;
			if (FindReadyNodeAndExcute(batch))
				it = all_batchs.erase(it);
			else
				it++;
		}
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
void RenderGraph::Clear() {
	_passes.clear();
	_ownedPasses.clear();
	_sortedPasses.clear();
	_lifecycles.clear();
	_needsCompile = true;
	_compiledVersion++;
}

PassNode* RenderGraph::GetPass(const std::string& name) const {
	for (auto* pass : _passes) {
		if (pass->GetName() == name) {
			return pass;
		}
	}
	return nullptr;
}

// 配置
void RenderGraph::SetPassEnabled(const std::string& name, bool enabled) {
	for (auto* pass : _passes) {
		if (pass->GetName() == name) {
			pass->SetEnable(enabled);
			return;
		}
	}
}

void RenderGraph::SetRenderTargetFBO(GLuint fbo)
{
	_renderTargetFBO = fbo;
}

#include "OpenGLRenderEngine/RenderGraph/RenderGraph.h"

using namespace OpenGLRenderGraph;

RenderGraph::RenderGraph(const std::string& name)
	:_name(name)
{
	_frameParallelPool.start();

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

#include "OpenGLRenderEngine/General/GPUTimer.h"

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

	if (_needsCompile) {
		Compile();
	}

	std::vector<std::shared_ptr<ThreadPool::SubmitHandle<void>>> BeginHandles;
	for (auto& pass : _sortedPasses)
	{
		BeginHandles.push_back(std::move(_frameParallelPool.submit(
			[pass = pass, &state]()->void
			{
				pass->FrameBegin(state);
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

	auto ExcuteBatch = [&](int batchIndex, std::vector<int>& batchs)-> void
		{
			std::vector<RenderGraphResource> BatchLifeCycleResource;

			auto ExcutePass = [&](int passIndex)-> void
				{
					auto* pass = _sortedPasses[passIndex];

					if (pass->ShouldExecute(state))
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

						pass->Execute(ctx, state);
					}

					auto& passLifeTimeResource = pass->GetLifeCycleResource();
					BatchLifeCycleResource.insert(BatchLifeCycleResource.end(), passLifeTimeResource.begin(), passLifeTimeResource.end());
				};

			std::unordered_set<int> pendingSet;
			for (int idx : batchs) {
				pendingSet.insert(idx);
			}

			while (!pendingSet.empty())
			{
				for (auto it = pendingSet.begin(); it != pendingSet.end(); )
				{
					int idx = *it;
					auto& handle = BeginHandles[idx];

					if (handle->is_ready())
					{
						it = pendingSet.erase(it);
						ExcutePass(idx);
					}
					else {
						++it;
					}
				}
			}

			for (auto& res : BatchLifeCycleResource)
			{
				auto it = _lifecycles.find(res);
				if (it == _lifecycles.end())
					return;

				auto& lifecycle = it->second;
				if (lifecycle.lastBatch <= batchIndex)
				{
					_resManager.ReleaseTexture(res);
					//std::cout << std::format("release res [{}]\n", res.name);
				}
			}

		};

	int passIndex = 0;
	int batchIndex = -1;
	std::vector<int> batchs;
	while (GetBatch(passIndex, batchs, batchIndex))
	{
		ExcuteBatch(batchIndex, batchs);
		batchs.clear();
	}

	std::vector<std::shared_ptr<ThreadPool::SubmitHandle<void>>> EndHandles;
	for (auto& pass : _sortedPasses)
		BeginHandles.push_back(std::move(_frameParallelPool.submit([pass = pass, &state]()->void {pass->FrameEnd(state); })));
	for (auto& handle : EndHandles)
		handle->get();

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

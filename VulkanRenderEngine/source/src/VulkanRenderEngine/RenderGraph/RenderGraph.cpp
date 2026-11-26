#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderGraph/RenderGraph.h"
#include "CriticalSectionLock.h"
#include <algorithm>
#include <cmath>

using namespace RenderGraph;

namespace
{
	// 图形队列的时间戳能力。不支持（timestampValidBits == 0）时不做 GPU 计时
	struct GpuTimestampInfo
	{
		double periodNs = 0.0;		// 一个时间戳单位多少纳秒
		uint64_t mask = 0;			// 有效位掩码（差值要按它回绕）
	};

	bool QueryGpuTimestampInfo(GpuTimestampInfo& info)
	{
		auto device = VKCONTEXT->GetDevice();
		if (!device)
			return false;

		const float period = device->GetPhysicalDeviceProperties().properties.limits.timestampPeriod;
		const auto families = device->GetPhysicalDevice().getQueueFamilyProperties();
		const uint32_t family = device->GetGraphicsQueueFamily();
		if (family >= families.size() || period <= 0.0f)
			return false;

		const uint32_t bits = families[family].timestampValidBits;
		if (bits == 0)
			return false;

		info.periodNs = double(period);
		info.mask = bits >= 64 ? ~0ull : ((1ull << bits) - 1ull);
		return true;
	}
}

Graph::PassExecuteContext::PassExecuteContext() : dependency(0) {}

Graph::PassExecuteContext::PassExecuteContext(PassExecuteContext&& other) noexcept
	: node(other.node)
	, registry(std::move(other.registry))
	, enable(other.enable)
	, dependency(other.dependency.load(std::memory_order_relaxed))
	, nextIndexs(std::move(other.nextIndexs))
{}

Graph::Graph(const std::string& name)
	:_name(name),
	_lastCleanupTimeAccumulator(0)
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

	BuildTransientRefCounts();

	_needsCompile = false;
	_compiledVersion++;
	BuildProfileLayout();
}

void Graph::BuildTransientRefCounts()
{
	_transientResources.clear();
	_transientInitRefs.clear();
	_passTransientIds.assign(_sortedPasses.size(), {});

	std::unordered_map<RenderGraphResource, uint32_t> idOf;
	for (size_t passIdx = 0; passIdx < _sortedPasses.size(); ++passIdx)
	{
		auto& ids = _passTransientIds[passIdx];
		// GetLifeCycleResource = Input + InputOption + Output + Temp
		for (const auto& res : _sortedPasses[passIdx]->GetLifeCycleResource())
		{
			auto [it, inserted] = idOf.try_emplace(res, uint32_t(_transientResources.size()));
			if (inserted)
			{
				_transientResources.push_back(res);
				_transientInitRefs.push_back(0);
			}

			const uint32_t id = it->second;
			if (std::find(ids.begin(), ids.end(), id) != ids.end())
				continue;		// 同一 Pass 里读写同一资源只算一次
			ids.push_back(id);
			_transientInitRefs[id]++;
		}
	}
}

void Graph::BuildProfileLayout()
{
	auto layout = std::make_shared<RenderGraphProfileLayout>();
	layout->graphName = _name;
	layout->version = _compiledVersion;

	std::unordered_map<const PassNode*, uint32_t> indexOf;
	for (uint32_t i = 0; i < uint32_t(_sortedPasses.size()); ++i)
		indexOf[_sortedPasses[i]] = i;

	layout->nodes.reserve(_sortedPasses.size());
	for (auto* pass : _sortedPasses)
	{
		RenderGraphProfileLayout::Node node;
		node.name = pass->GetName();
		node.batch = pass->GetBatch();
		node.fence = !pass->_render;
		for (auto* prev : pass->_afters)
		{
			auto it = indexOf.find(prev);
			if (it != indexOf.end())
				node.afters.push_back(it->second);
		}
		std::sort(node.afters.begin(), node.afters.end());
		layout->nodes.push_back(std::move(node));
	}
	_profileLayout = std::move(layout);
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
	FrameLocalResourceRecord& frameLocalResRecord,
	ExternalResourceManager& externalResManager,
	std::atomic<uint32_t>& doneCounter,
	std::atomic<uint32_t>* transientRefs,
	std::shared_ptr<GraphSubmitContext>& submitCtx
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
					[&, idx, transientRefs, &ctx = passCtxs[idx], &passCtxs = passCtxs]()->void {
						RenderFrameProfile* profile = state.profile.get();
						RenderPassProfile* passProfile = (profile && size_t(idx) < profile->passes.size()) ? &profile->passes[idx] : nullptr;
						const int64_t start = passProfile ? RenderProfileNowUs() : 0;
						const uint32_t gpuQuery = (passProfile && submitCtx->queryPool) ? uint32_t(idx) * 2u : UINT32_MAX;
						const bool executed = ExecutePass(ctx.node, ctx.registry, state, resPrefix, frameLocalResRecord, externalResManager, submitCtx,
							passProfile ? &passProfile->executeWaitUs : nullptr, gpuQuery);
						if (passProfile)
						{
							passProfile->executeUs = RenderProfileNowUs() - start;
							passProfile->executed = executed;
						}
						// 先归还资源再放行后继：后继 Pass 取资源时能直接复用刚还回池子的纹理
						for (uint32_t id : _passTransientIds[idx])
						{
							if (transientRefs[id].fetch_sub(1, std::memory_order_acq_rel) == 1)
								_resManager.ReleaseTexture(_transientResources[id], resPrefix);
						}
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
			it = batchdata.passes.erase(it);
			continue;
		}
		++it;
	}

	if (batchdata.passes.empty())
		batchdata.isEnd = true;

}

bool Graph::ExecutePass(
	PassNode* node,
	FrameDataRegistry& registry,
	RenderState& state,
	const std::string& resPrefix,
	FrameLocalResourceRecord& frameLocalResRecord,
	ExternalResourceManager& externalResManager,
	std::shared_ptr<GraphSubmitContext>& submitCtx,
	int64_t* executeWaitUs,
	uint32_t gpuQuery
)
{
	//auto start = Tool::GetTimestampMircoseconds();
	//std::cout << std::format("ExecutePass {}, threadid = {}\n", node->GetName(), std::this_thread::get_id());

	if (!node->ShouldExecute(registry, state))
		return false;

	PassFrameCmdContext cmdCtx(submitCtx);
	cmdCtx.SetExecuteWaitRecord(executeWaitUs);
	if (gpuQuery != UINT32_MAX)
		cmdCtx.SetGpuTimestampQuery(gpuQuery);
	PassFrameContext ctx;
	ctx.passName = node->GetName();

	static auto IsVolumeResource = [](const RenderGraphResource& res)->bool
		{
			if (auto desc = std::get_if<TextureDesc>(&res.desc))
				return desc->IsVolume();
			return false;
		};

	auto FetchTexture = [&](const RenderGraphResource& res, const std::string& prefix, const RenderGraphResourceLayout& layout, bool optional,
		std::vector<TextureSlot>& out)->void
		{
			TextureSlot slot;
			if (IsVolumeResource(res))
				slot.Set(optional ? _resManager.TryGetVolume(res, prefix) : _resManager.GetVolume(res, prefix));
			else
				slot.Set(optional ? _resManager.TryGetTexture(res, prefix) : _resManager.GetTexture(res, prefix));

			// 谁被设进槽位就推谁，不再需要自己判断维度
			if (auto tex3 = slot.As<Texture3D>())
				cmdCtx.PushTexWithLayout(tex3, layout);
			else if (auto tex2 = slot.As<Texture2D>())
				cmdCtx.PushTexWithLayout(tex2, layout);

			out.push_back(slot);
		};

	for (const auto& input : node->GetInputs())
		FetchTexture(input.resource, resPrefix, input.layout, false, ctx.inputs);

	for (const auto& input : node->GetInputOptions())
		FetchTexture(input.resource, resPrefix, input.layout, true, ctx.optionInputs);

	for (const auto& output : node->GetOutputs())
		FetchTexture(output.resource, resPrefix, output.layout, false, ctx.outputs);

	for (const auto& temp : node->GetTemps())
		FetchTexture(temp.resource, resPrefix, temp.layout, false, ctx.temps);

	for (const auto& persitent : node->GetPersistents())
		FetchTexture(persitent.resource, "", persitent.layout, false, ctx.persistents);

	for (const auto& persitent : node->GetPersistentOptions())
		FetchTexture(persitent.resource, "", persitent.layout, true, ctx.optionPersistents);

	for (const auto& external : node->GetExternals()) {
		if (external.resource.type == ResourceType::Texture)
		{
			TextureSlot slot;
			if (auto tex = externalResManager.GetExternalTexture(external.resource.name))
			{
				slot.Set(tex);
				cmdCtx.PushTexWithLayout(tex, external.layout);
			}
			ctx.externals.push_back(slot);
		}
	}

	for (const auto& framelocal : node->GetFrameLocal()) {
		FetchTexture(framelocal.resource, resPrefix, framelocal.layout, false, ctx.frameLocals);
		frameLocalResRecord.AddTexture(framelocal.resource);
	}

	node->Execute(cmdCtx, registry, ctx, state);

	//std::cout << std::format("ExecutePass {} ,cost {}ms\n", pass->GetName(), Tool::GetTimestampMircoseconds() - start);
	return true;
}


// 执行
void Graph::Execute(
	std::shared_ptr<RenderState>& state,
	const std::unordered_map<std::string, std::shared_ptr<Texture2D>>& externalResources
)
{
	if (!state)
		return;

	// 本次 Execute 的提交上下文：提交锁 + 提交链
	auto submitCtx = std::make_shared<GraphSubmitContext>();
	submitCtx->timeline = std::make_shared<VKWrapper::VKTimelineSemaphore>(VKCONTEXT->GetDevice().get());
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

	// 渲染性能统计：每个 Pass 一个槽位，按排序后的下标，各线程只写自己的槽位
	RenderFrameProfile* profile = state->profile.get();
	if (profile)
	{
		profile->graphLayout = _profileLayout;
		profile->passes.assign(_sortedPasses.size(), RenderPassProfile{});
	}

	// GPU 计时：每个 Pass 两个时间戳（开始 / 结束）。查询池在链首重置，收尾等链做完后读回、销毁
	GpuTimestampInfo gpuTimestamp;
	const uint32_t gpuQueryCount = uint32_t(_sortedPasses.size()) * 2u;
	if (profile && gpuQueryCount > 0 && QueryGpuTimestampInfo(gpuTimestamp))
	{
		vk::QueryPoolCreateInfo poolInfo;
		poolInfo
			.setQueryType(vk::QueryType::eTimestamp)
			.setQueryCount(gpuQueryCount);
		auto [poolResult, pool] = VKCONTEXT->GetDeviceHandle().createQueryPool(poolInfo);
		if (poolResult == vk::Result::eSuccess)
		{
			submitCtx->mutex.lock();
			auto resetCmd = VKCONTEXT->GetCommandBuffer();
			resetCmd->resetQueryPool(pool, 0, gpuQueryCount);
			const bool resetSubmitted = submitCtx->Submit(resetCmd) != 0;
			submitCtx->mutex.unlock();

			// 没重置成功就不能写时间戳：这一帧不做 GPU 计时（链上还没有用到它的命令，可以直接销毁）
			if (resetSubmitted)
				submitCtx->queryPool = pool;
			else
				VKCONTEXT->GetDeviceHandle().destroyQueryPool(pool);
		}
	}

	std::vector<std::shared_ptr<ThreadPool::SubmitHandle<void>>> BeginHandles;
	BeginHandles.reserve(passExeContext.size());
	for (size_t passIdx = 0; passIdx < passExeContext.size(); ++passIdx)
	{
		auto& passCtx = passExeContext[passIdx];
		BeginHandles.push_back(std::move(_frameParallelPool.submit(
			[&beginCounter, node = passCtx.node, registryPtr = &passCtx.registry, &state, profile, passIdx]()->void
			{
				const int64_t start = profile ? RenderProfileNowUs() : 0;
				node->FrameBegin(*registryPtr, *state);
				if (profile)
					profile->passes[passIdx].frameBeginUs = RenderProfileNowUs() - start;
				beginCounter++;
			})
		));
	}


	auto resPrefix = Tool::GenerateSimpleUuid();
	ExternalResourceManager externalResManager(externalResources);
	FrameLocalResourceRecord frameLocalResRecord;

	std::vector<BatchData> running_batchs;
	running_batchs.reserve(_sortedPasses.size());

	int passIndex = 0;
	int batchIndex = -1;
	std::vector<BatchData::PassData> batchpasses;
	while (GetBatch(passIndex, batchpasses, batchIndex))
	{
		running_batchs.push_back(BatchData{ .batchIndex = batchIndex, .passes = std::move(batchpasses) });
		batchpasses.clear();
	}

	// 临时资源引用计数（本帧一份）
	const size_t transientCount = _transientResources.size();
	std::unique_ptr<std::atomic<uint32_t>[]> transientRefs(new std::atomic<uint32_t>[std::max<size_t>(transientCount, 1)]);
	for (size_t i = 0; i < transientCount; ++i)
		transientRefs[i].store(_transientInitRefs[i], std::memory_order_relaxed);

	uint32_t targetDoneCounter = passExeContext.size();
	uint32_t lastBeginCounter = std::numeric_limits<uint32_t>::max();
	uint32_t lastDoneCounter = std::numeric_limits<uint32_t>::max();
	while (!running_batchs.empty())
	{
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
				FindReadyNodeAndExcute(BeginHandles, batch, passExeContext, *state, resPrefix, frameLocalResRecord, externalResManager, doneCounter, transientRefs.get(), submitCtx);

			if (batch.isEnd)
				it = running_batchs.erase(it);
			else
				it++;
		}

	}

	// 兜底：正常情况下每个临时资源都在最后一个使用者执行完时已经归还
	for (size_t i = 0; i < transientCount; ++i)
	{
		if (transientRefs[i].load(std::memory_order_relaxed) != 0)
			_resManager.ReleaseTexture(_transientResources[i], resPrefix);
	}

	// 收尾：链尾提交一条全局屏障，再等整条链在 GPU 上做完才返回
	//   * 图之后的 Combin / Bloom / PostProcess 直接用 VKCONTEXT 提交、不在链上：屏障让图内的写入对它们可见
	//     （CPU 等到 GPU 做完只保证执行完成，不保证之后 GPU 上的读能看到这些写）；
	//   * 下一次 Execute 的 FrameBegin 会改写长期持有的 UBO、销毁重建 TLAS/BLAS，纹理池跨帧复用纹理，
	//     Texture2D::Resize / 历史纹理重建会立刻销毁 ImageView，Resize 重建图时会直接析构本图 —— 都要求这里已经做完
	{
		submitCtx->mutex.lock();
		auto tailCmd = VKCONTEXT->GetCommandBuffer();
		tailCmd->pipelineBarrier(
			vk::PipelineStageFlagBits::eAllCommands,
			vk::PipelineStageFlagBits::eAllCommands,
			vk::MemoryBarrier(vk::AccessFlagBits::eMemoryWrite, vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite));
		submitCtx->Submit(tailCmd);
		const uint64_t lastValue = submitCtx->value;
		submitCtx->mutex.unlock();

		if (lastValue > 0)
			submitCtx->timeline->Wait(lastValue);
	}

	// GPU 计时读回：链已经做完，查询结果都可用（没提交过命令的 Pass 没写时间戳，可用标记为 0）
	if (submitCtx->queryPool)
	{
		struct QueryResult { uint64_t value; uint64_t available; };
		std::vector<QueryResult> results(gpuQueryCount, QueryResult{ 0, 0 });
		const vk::Result readResult = VKCONTEXT->GetDeviceHandle().getQueryPoolResults(
			submitCtx->queryPool, 0, gpuQueryCount,
			results.size() * sizeof(QueryResult), results.data(), sizeof(QueryResult),
			vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability);

		// eNotReady：有查询没写（Pass 没执行 / 没提交），其余照常可用
		if (profile && (readResult == vk::Result::eSuccess || readResult == vk::Result::eNotReady))
		{
			const size_t passCount = std::min(profile->passes.size(), _sortedPasses.size());
			for (size_t i = 0; i < passCount; ++i)
			{
				const auto& begin = results[i * 2];
				const auto& end = results[i * 2 + 1];
				if (!begin.available || !end.available)
					continue;
				const uint64_t ticks = (end.value - begin.value) & gpuTimestamp.mask;
				auto& pp = profile->passes[i];
				pp.gpuUs = int64_t(std::llround(double(ticks) * gpuTimestamp.periodNs / 1000.0));
				pp.gpuValid = true;
			}
		}

		VKCONTEXT->GetDeviceHandle().destroyQueryPool(submitCtx->queryPool);
		submitCtx->queryPool = VK_NULL_HANDLE;
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

	for (auto& res : frameLocalResRecord.GetRes())
		_resManager.ReleaseTexture(res, resPrefix);

	//std::cout << std::format("Execute done {}\n", frameIndex);
}

// 清空
void Graph::Clear() {
	_profileLayout.reset();
	_passes.clear();
	_sortedPasses.clear();
	_ownedPasses.clear();
	_lifecycles.clear();
	_transientResources.clear();
	_transientInitRefs.clear();
	_passTransientIds.clear();
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

uint32_t Graph::InheritPersistentResources(Graph& old)
{
	if (&old == this)
		return 0;
	uint32_t count = 0;
	for (auto* pass : _passes)
	{
		for (const auto& res : pass->GetPersistents())
			count += _resManager.AdoptTexture(old._resManager, res.resource) ? 1u : 0u;
		for (const auto& res : pass->GetPersistentOptions())
			count += _resManager.AdoptTexture(old._resManager, res.resource) ? 1u : 0u;
	}
	return count;
}

void Graph::CleanGraphIdleResource()
{
	auto time = Tool::GetTimestampSecond();
	if (time - _lastCleanupTimeAccumulator <= _CleanupThresold)
		return;

	if (_lastCleanupTimeAccumulator != 0)
		_resManager.CleanupIdleResource();
	_lastCleanupTimeAccumulator = Tool::GetTimestampSecond();
}

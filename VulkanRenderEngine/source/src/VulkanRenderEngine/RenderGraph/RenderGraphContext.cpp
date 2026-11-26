#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderGraph/RenderGraphContext.h"
#include "VulkanRenderEngine/General/RenderProfiler.h"

using namespace RenderGraph;

PassFrameCmd::PassFrameCmd(PassFrameCmdContext* ctx)
	:_ctx(ctx) {}

PassFrameCmd::PassFrameCmd(PassFrameCmd&& other)
	: VKWrapper::VKCommandBuffer(std::move(other)) {
	_ctx = other._ctx;
}

void PassFrameCmd::SubmitToQueue(const CmdSyncSeamphore& syncSeamphore, std::shared_ptr<VKWrapper::VKFence> signalFence) {
	auto temp = std::make_shared<PassFrameCmd>(std::move(*this));

	if (_ctx)
		_ctx->SubmitToQueue(temp, syncSeamphore, signalFence);

	if (!VKCONTEXT->GetCommandBuffer(shared_from_this()))
		std::cerr << std::format("[ PassFrameCmd ] GetCommandBuffer from self fail!\n");
}

void PassFrameCmd::SubmitNow(const CmdSyncSeamphore& syncSeamphore, std::shared_ptr<VKWrapper::VKFence> signalFence) {
	auto temp = std::make_shared<PassFrameCmd>(std::move(*this));

	if (_ctx)
		_ctx->SubmitNow(temp, syncSeamphore, signalFence);

	if (!VKCONTEXT->GetCommandBuffer(shared_from_this()))
		std::cerr << std::format("[ PassFrameCmd ] GetCommandBuffer from self fail!\n");
}

void PassFrameCmd::SubmitNowAndWait(const CmdSyncSeamphore& syncSeamphore) {
	auto temp = std::make_shared<PassFrameCmd>(std::move(*this));

	if (_ctx)
		_ctx->SubmitNowAndWait(temp, syncSeamphore);

	if (!VKCONTEXT->GetCommandBuffer(shared_from_this()))
		std::cerr << std::format("[ PassFrameCmd ] GetCommandBuffer from self fail!\n");
}

// 调试开关：true = 每个 Pass 提交后 CPU 等它在 GPU 上做完（改动前的行为），用于对比帧时间、排查问题
static constexpr bool kWaitEachPassOnGPU = false;

uint64_t GraphSubmitContext::Submit(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, CmdSyncSeamphore syncSeamphore, std::shared_ptr<VKWrapper::VKFence> signalFence)
{
	const uint64_t signalValue = value + 1;
	if (value > 0)
		syncSeamphore.waitSemaphores.push_back(WaitSemaphoreData{ .semaphore = timeline, .flags = vk::PipelineStageFlagBits::eAllCommands, .value = value });
	syncSeamphore.signalSemaphores.push_back(SignalSemaphoreData{ .semaphore = timeline, .value = signalValue });

	if (VKCONTEXT->SubmitCommandImmediately(cmd, syncSeamphore, signalFence) != vk::Result::eSuccess)
		return 0;

	value = signalValue;
	return value;
}

PassFrameCmdContext::PassFrameCmdContext(const std::shared_ptr<GraphSubmitContext>& submitCtx)
	:_isEnd(false), _immediatelySubmit(false), _submitCtx(submitCtx)
{}

PassFrameCmdContext::~PassFrameCmdContext()
{}

std::shared_ptr<PassFrameCmd> PassFrameCmdContext::GetCmd()
{
	auto cmd = std::make_shared<PassFrameCmd>(this);
	if (VKCONTEXT->GetCommandBuffer(cmd))
		return cmd;
	return nullptr;
}

void PassFrameCmdContext::SubmitToQueue(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, const CmdSyncSeamphore& syncSeamphore, std::shared_ptr<VKWrapper::VKFence> signalFence) {
	if (_immediatelySubmit)
		SubmitNow(cmd, syncSeamphore, signalFence);
	else
		_submitcmds.push(PassFrameSubmitCMDData{ .cmd = cmd, .syncSeamphore = syncSeamphore ,.signalFence = signalFence });
}

// 各 Pass 的命令按各自录制时预设的纹理布局执行，「补差 barrier + 提交」必须一组一组按顺序来：
// 拿到锁 = 前面的组已经接到提交链上（不要求已在 GPU 上做完），轮到自己
void PassFrameCmdContext::LockForExecute()
{
	if (_executeWaitUs)
	{
		const int64_t start = RenderProfileNowUs();
		_submitCtx->mutex.lock();
		*_executeWaitUs += RenderProfileNowUs() - start;
	}
	else
		_submitCtx->mutex.lock();
}

void PassFrameCmdContext::SubmitNow(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, const CmdSyncSeamphore& syncSeamphore, std::shared_ptr<VKWrapper::VKFence> signalFence)
{
	if (!_immediatelySubmit)
	{
		LockForExecute();
		_immediatelySubmit = true;
	}
	_submitcmds.push(PassFrameSubmitCMDData{ .cmd = cmd, .syncSeamphore = syncSeamphore ,.signalFence = signalFence });
	StartupCmd();
}

void PassFrameCmdContext::SubmitNowAndWait(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, const CmdSyncSeamphore& syncSeamphore)
{
	if (!_immediatelySubmit)
	{
		LockForExecute();
		_immediatelySubmit = true;
	}
	_submitcmds.push(PassFrameSubmitCMDData{ .cmd = cmd, .syncSeamphore = syncSeamphore });
	StartupCmd();

	// 回读：等本 Pass 这次提交做完（timeline 的 signal 覆盖队列里所有更早的提交，前面的 Pass 也一定做完了）
	if (_lastSignaled > 0)
		_submitCtx->timeline->Wait(_lastSignaled);
}

void RenderGraph::PassFrameCmdContext::Start()
{
	VKWrapper::PassImageStateRecord::StartRecord();
	for (auto& data : _texDatas)
	{
		auto img = data.image;
		auto* texLayoutData = std::get_if<TextureLayout>(&data.layout.data);
		VKWrapper::PassImageStateRecord::AddState(img, texLayoutData->stage, texLayoutData->usage);
	}
}

void RenderGraph::PassFrameCmdContext::End()
{
	if (_isEnd)
		return;

	_isEnd = true;

	// 只保证「补差 + 提交」按顺序，不再等 GPU 做完：Pass 之间的先后与可见性由提交链保证
	if (!_immediatelySubmit)
	{
		if (!_submitcmds.empty())
		{
			LockForExecute();
			StartupCmd();
			if (kWaitEachPassOnGPU && _lastSignaled > 0)
				_submitCtx->timeline->Wait(_lastSignaled);
			_submitCtx->mutex.unlock();
		}
	}
	else
	{
		if (!_submitcmds.empty())
			StartupCmd();
		if (kWaitEachPassOnGPU && _lastSignaled > 0)
			_submitCtx->timeline->Wait(_lastSignaled);
		_submitCtx->mutex.unlock();
	}

}

// 调用方持有 _submitCtx->mutex
void PassFrameCmdContext::StartupCmd()
{
	Need();

	while (!_submitcmds.empty())
	{
		auto& cmdData = _submitcmds.front();
		if (auto v = _submitCtx->Submit(cmdData.cmd, std::move(cmdData.syncSeamphore), cmdData.signalFence))
			_lastSignaled = v;
		_submitcmds.pop();
	}
}

// 本 Pass 第一次提交前：补差 barrier，接在链尾。调用方持有 _submitCtx->mutex
void PassFrameCmdContext::Need()
{
	if (_started)
		return;
	_started = true;

	auto barriercmd = VKCONTEXT->GetCommandBuffer();

	// FrameBegin、图之前的阶段直接用 VKCONTEXT 提交的工作不在链上（它们已经 SubmitNowAndWait 做完），
	// 一条全局屏障让它们的写入对本 Pass 可见
	barriercmd->pipelineBarrier(
		vk::PipelineStageFlagBits::eAllCommands,
		vk::PipelineStageFlagBits::eAllCommands,
		vk::MemoryBarrier(vk::AccessFlagBits::eMemoryWrite, vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite));

	// 真实态 -> 本 Pass 声明态
	VKWrapper::PassImageStateRecord::EndRecord(barriercmd);

	if (auto v = _submitCtx->Submit(barriercmd))
		_lastSignaled = v;
}

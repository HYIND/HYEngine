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

PassFrameCmdContext::PassFrameCmdContext(const std::shared_ptr<CriticalSectionLock>& mutex)
	:_mutex(mutex), _immediatelySubmit(false), _isEnd(false)
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

// 各 Pass 的命令按各自录制时预设的纹理布局执行，同一张图里只能一组一组串行执行：
// 拿到锁 = 前面的组已经在 GPU 上执行完，轮到自己
void PassFrameCmdContext::LockForExecute()
{
	if (_executeWaitUs)
	{
		const int64_t start = RenderProfileNowUs();
		_mutex->lock();
		*_executeWaitUs += RenderProfileNowUs() - start;
	}
	else
		_mutex->lock();
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

	if (_timeLine && cmdcount > 0)
		_timeLine->Wait(cmdcount);
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

	if (!_immediatelySubmit)
	{
		if (!_submitcmds.empty())
		{
			LockForExecute();
			StartupCmd();
			if (_timeLine && cmdcount > 0)
				_timeLine->Wait(cmdcount);
			_mutex->unlock();
		}
	}
	else
	{
		if (!_submitcmds.empty())
			StartupCmd();
		if (_timeLine && cmdcount > 0)
			_timeLine->Wait(cmdcount);
		_mutex->unlock();
	}

}

void PassFrameCmdContext::StartupCmd()
{
	Need();

	while (!_submitcmds.empty())
	{
		auto& cmdData = _submitcmds.front();
		cmdData.syncSeamphore.waitSemaphores.push_back(WaitSemaphoreData{ .semaphore = _timeLine, .flags = vk::PipelineStageFlagBits::eAllCommands , .value = cmdcount });
		cmdData.syncSeamphore.signalSemaphores.push_back(SignalSemaphoreData{ .semaphore = _timeLine, .value = ++cmdcount });
		VKCONTEXT->SubmitCommandImmediately(cmdData.cmd, cmdData.syncSeamphore, cmdData.signalFence);
		_submitcmds.pop();
	}
}

void PassFrameCmdContext::Need()
{
	if (cmdcount > 0)
		return;

	if (!_timeLine && cmdcount == 0)
	{
		_timeLine = std::make_shared<VKWrapper::VKTimelineSemaphore>(VKCONTEXT->GetDevice().get());
		auto barriercmd = VKCONTEXT->GetCommandBuffer();
		VKWrapper::PassImageStateRecord::EndRecord(barriercmd);
		//for (auto& data : _texDatas)
		//{
		//	auto* TexLayoutData = std::get_if<TextureLayout>(&data.layout.data);
		//	if (TexLayoutData)
		//		data.tex->TransitionLayout(barriercmd, nullptr, TexLayoutData->stage, TexLayoutData->usage);
		//}

		if (barriercmd->IsRecording())
		{
			CmdSyncSeamphore sync{ .signalSemaphores = {SignalSemaphoreData{.semaphore = _timeLine, .value = ++cmdcount }} };
			VKCONTEXT->SubmitCommandImmediately(barriercmd, sync);
		}
	}
}
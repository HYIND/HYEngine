#include "vkstdafx.h"
#include "VulkanRenderEngine/VKFramePipeline.h"

#include <algorithm>
#include <format>
#include <iostream>
#include <limits>

void FlightSync::WaitForNextProgress()
{
	++curProgress;
	if (preFrameTimeLine)
		preFrameTimeLine->Wait(curProgress);
}

void FlightSync::SignalDoneProgress()
{
	curframeTimeLine->Signal(curProgress);
}

VKFramePipeline::VKFramePipeline()
	:_frameTaskPool(std::max(std::max(1u, GlobalConfig::MaxFramesInFlight), std::thread::hardware_concurrency()))
{
	_maxFramesInFlight = std::max(1u, GlobalConfig::MaxFramesInFlight);
}

VKFramePipeline::~VKFramePipeline()
{
	Stop();
}

void VKFramePipeline::AddStage(std::shared_ptr<IFrameStage> stage)
{
	if (!stage)
		return;

	if (!_stop)
	{
		std::cerr << "[ VKFramePipeline ] AddStage 必须在流水线停着的时候调用，已忽略\n";
		return;
	}

	if (IsTargetReady())
		stage->OnResize(_width, _height, _maxFramesInFlight);
	_stages.push_back(std::move(stage));
}

void VKFramePipeline::InitFinalColors()
{
	_finalColors.clear();

	const TextureConfig config
	{
		.minFilter = vk::Filter::eLinear,
		.magFilter = vk::Filter::eLinear,
		.wrapU = vk::SamplerAddressMode::eClampToEdge,
		.wrapV = vk::SamplerAddressMode::eClampToEdge,
		.anisotropy = false,
		.gammaCorrection = false
	};

	for (uint32_t i = 0; i < GetSlotCount(); i++)
		_finalColors.push_back(std::make_shared<Texture2D>(_width, _height, vk::Format::eR8G8B8A8Unorm, config));
}

bool VKFramePipeline::Resize(uint32_t width, uint32_t height)
{
	width = std::max(1u, width);
	height = std::max(1u, height);
	if (_width == width && _height == height)
		return true;

	const bool needRestart = !_stop;
	if (needRestart)
		Stop();

	// 旧尺寸的帧不要了：在途的已经随 Stop 画完，完成队列里的宿主也不该再拿（尺寸不对）
	_runningFrames.clear();
	{
		LockGuard guard(_doneFramesMutex);
		_doneFrames.clear();
		_hostSlot = -1;
	}

	_width = width;
	_height = height;
	InitFinalColors();

	for (auto& stage : _stages)
		stage->OnResize(_width, _height, _maxFramesInFlight);

	if (needRestart)
		Run();
	return true;
}

void VKFramePipeline::PushFrame(FrameRequest request)
{
	if (!request.state)
		return;

	{
		LockGuard guard(_candidateFramesMutex);
		if (_candidateFrames.size() >= _maxFramesInFlight)
		{
			if (_renderProfiler->IsEnabled())
				_renderProfiler->AddDropped();
			_candidateFrames.pop();
		}
		_candidateFrames.push(std::move(request));
	}

	LockGuard guard(_executeMutex);
	_executeCV.NotifyOne();
}

bool VKFramePipeline::HasPendingFrame()
{
	LockGuard guard(_candidateFramesMutex);
	return !_candidateFrames.empty();
}

std::shared_ptr<FrameJob> VKFramePipeline::PopDoneFrame(bool wait)
{
	LockGuard guard(_doneFramesMutex);
	while (_doneFrames.empty())
	{
		if (!wait)
			return nullptr;
		_doneFramesCV.Wait(guard);
		if (_stop)
			return nullptr;
	}

	auto job = std::move(_doneFrames.front());
	_doneFrames.pop_front();
	// 这一帧交给宿主了：它的最终画面在宿主下一次取帧之前不能被新帧复用
	_hostSlot = job ? static_cast<int64_t>(job->slot) : -1;
	return job;
}

void VKFramePipeline::WaitImage(const ImageCallback& callback)
{
	auto job = PopDoneFrame(true);
	if (callback && job && job->finalColor)
		callback(job->finalColor, job->state);
}

bool VKFramePipeline::FetchImage(const ImageCallback& callback)
{
	auto job = PopDoneFrame(false);
	if (!job)
		return false;
	if (callback && job->finalColor)
		callback(job->finalColor, job->state);
	return true;
}

void VKFramePipeline::Run()
{
	if (!_stop)
		return;

	_frameTaskPool.start();
	_stop = false;
	_exeLoopThread = std::make_shared<std::thread>(&VKFramePipeline::ExecuteLoop, this);
}

void VKFramePipeline::Stop()
{
	if (_stop)
		return;

	_stop = true;
	_frameTaskPool.stop();

	{
		LockGuard guard(_doneFramesMutex);
		_doneFramesCV.NotifyAll();
	}

	{
		LockGuard guard(_executeMutex);
		_executeCV.NotifyAll();
	}

	if (_exeLoopThread)
	{
		if (_exeLoopThread->joinable())
			_exeLoopThread->join();
		_exeLoopThread.reset();
	}
}

std::shared_ptr<FrameJob> VKFramePipeline::MakeJob(FrameRequest request)
{
	const FrameJob* prev = !_runningFrames.empty() ? _runningFrames.back().get() : nullptr;

	auto job = std::make_shared<FrameJob>();
	job->frameIndex = _frameIndex++;

	// 槽位：绕开在途帧、完成队列里的帧和宿主手上的那一帧。
	// 在途 < MaxFramesInFlight（不然不会派发）、完成队列 ≤ MaxFramesInFlight、宿主 1 帧，
	// 槽位有 2 * MaxFramesInFlight + 1 个，所以一定有空的
	{
		std::vector<bool> slotUsed(GetSlotCount(), false);
		std::vector<bool> flightUsed(_maxFramesInFlight, false);
		for (auto& running : _runningFrames)
		{
			slotUsed[running->slot] = true;
			flightUsed[running->flightIndex] = true;
		}
		{
			LockGuard guard(_doneFramesMutex);
			for (auto& done : _doneFrames)
				slotUsed[done->slot] = true;
			if (_hostSlot >= 0 && _hostSlot < static_cast<int64_t>(slotUsed.size()))
				slotUsed[static_cast<size_t>(_hostSlot)] = true;
		}
		job->slot = static_cast<uint32_t>(std::find(slotUsed.begin(), slotUsed.end(), false) - slotUsed.begin());
		job->flightIndex = static_cast<uint32_t>(std::find(flightUsed.begin(), flightUsed.end(), false) - flightUsed.begin());
		job->slot = std::min<uint32_t>(job->slot, GetSlotCount() - 1);
		job->flightIndex = std::min<uint32_t>(job->flightIndex, _maxFramesInFlight - 1);
	}

	job->finalColor = _finalColors[job->slot];
	job->state = std::move(request.state);
	job->stageData = std::move(request.stageData);
	job->stageData.resize(_stages.size());

	job->sync = std::make_shared<FlightSync>();
	job->sync->curframeTimeLine = std::make_shared<VKWrapper::VKTimelineSemaphore>(VKCONTEXT->GetDevice().get());
	job->sync->preFrameTimeLine = prev ? prev->sync->curframeTimeLine : nullptr;

	auto& renderRecord = job->state->renderRecord;
	renderRecord.frameIndex = job->frameIndex;
	renderRecord.frameStartMicroTimeStamp = Tool::GetTimestampMircoseconds();

	// 渲染性能统计：开着才给这一帧挂 profile（Renderer3D 那边开着时已经挂上并记了帧数据解析）
	if (_renderProfiler->IsEnabled())
	{
		auto& profile = job->state->profile;
		if (!profile)
			profile = std::make_shared<RenderFrameProfile>();
		const int64_t now = RenderProfileNowUs();
		if (profile->pushTimeUs > 0)
			profile->Add(RenderProfileStep::Queue, now - profile->pushTimeUs);
		profile->startTimeUs = now;
		profile->frameIndex = job->frameIndex;
	}
	else
		job->state->profile.reset();

	for (uint32_t i = 0; i < _stages.size(); i++)
		_stages[i]->OnDispatch(*job, prev, i);

	return job;
}

void VKFramePipeline::ExecuteFrame(FrameJob& job)
{
	auto& sync = *job.sync;

	// 渲染性能统计：各段先等上一帧放行（单独记在 WaitPrevFrame），计时从 guard 之后开始
	RenderFrameProfile* profile = job.Profile();
	sync.waitAccumUs = profile ? &profile->steps[size_t(RenderProfileStep::WaitPrevFrame)] : nullptr;

	// 段 1：提交图外 SubmitToQueue 攒下的命令，再回收延迟释放的资源。这是整帧的事，不属于哪个阶段
	{
		FlightSync::ProgressScope syncScope(sync);
		RenderProfileScope scope(profile, RenderProfileStep::ProcessRetire);
		VKCONTEXT->ProcessPendingCommand();
		VKCONTEXT->ProcessRetireAndPushFrameIndex();
	}

	// 各阶段按注册顺序在这个线程上串行执行，各自进出自己的段
	for (uint32_t i = 0; i < _stages.size(); i++)
		_stages[i]->Execute(job, i);

	sync.waitAccumUs = nullptr;
	job.state->renderRecord.frameEndMicroTimeStamp = Tool::GetTimestampMircoseconds();

	// 段数检查：每帧段数必须一样，否则前后帧的段对不上号
	const uint32_t segments = sync.curProgress;
	uint32_t expected = 0;
	if (!_segmentCount.compare_exchange_strong(expected, segments) && expected != segments)
	{
		if (!_segmentWarned.exchange(true))
			std::cerr << std::format("[ VKFramePipeline ] WARNING: 第 {} 帧进出了 {} 段，之前的帧是 {} 段；"
				"每个阶段每帧进出的段数必须固定（不画的段也要进出一次）\n", job.frameIndex, segments, expected);
	}

	// 兜底：这一帧做完了，把时间线放到很大，后一帧不管还剩几段都不用再等它
	sync.curframeTimeLine->Signal(std::numeric_limits<uint32_t>::max() / 2u);
}

void VKFramePipeline::ExecuteLoop()
{
	SetThreadDescription(GetCurrentThread(), L"VKFramePipeline::ExecuteLoop");

	while (!_stop || !_runningFrames.empty())
	{
		for (auto& stage : _stages)
			stage->OnTick();

		bool didWork = false;

		if (!_stop && IsTargetReady())
		{
			LockGuard guard(_candidateFramesMutex);
			while (!_candidateFrames.empty() && (_runningFrames.size() < _maxFramesInFlight))
			{
				auto job = MakeJob(std::move(_candidateFrames.front()));
				_candidateFrames.pop();

				job->handle = _frameTaskPool.submit([this, weakJob = std::weak_ptr(job)]()->void
					{
						if (auto job = weakJob.lock())
							ExecuteFrame(*job);

						LockGuard guard(_executeMutex);
						_executeCV.NotifyOne();
					});
				_runningFrames.push_back(std::move(job));
				didWork = true;
			}
		}

		while (!_runningFrames.empty() && _runningFrames.front()->handle->is_ready())
		{
			auto job = std::move(_runningFrames.front());
			_runningFrames.pop_front();
			if (RenderFrameProfile* profile = job->Profile())
			{
				profile->endTimeUs = RenderProfileNowUs();
				_renderProfiler->Publish(std::move(job->state->profile));
			}

			{
				LockGuard guard(_doneFramesMutex);
				if (_doneFrames.size() >= _maxFramesInFlight)
					_doneFrames.pop_front();
				_doneFrames.push_back(std::move(job));
				_doneFramesCV.NotifyAll();
			}

			didWork = true;
		}

		if (!didWork)
		{
			LockGuard guard(_executeMutex);
			_executeCV.WaitFor(guard, std::chrono::milliseconds(20));
		}
	}
}

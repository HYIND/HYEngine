#include "Helper/DynamicFpsController.h"
#include <algorithm>
#include <thread>
#include <cmath>

DynamicFpsController::DynamicFpsController(uint32_t gameTargetFps)
{
	setGameTargetFps(gameTargetFps);
	setCurTargetFps(gameTargetFps);
}

void DynamicFpsController::setCurTargetFps(uint32_t curTargetFps)
{
	if (curTargetFps == _curTargetFps)
		return;
	_curTargetFps = std::max(1u, curTargetFps);
	_curTimeInOneFps = 1000.f / _curTargetFps;
}

void DynamicFpsController::setGameTargetFps(uint32_t gameFps)
{
	_gameTargetFps = std::max(1u, gameFps);
}

uint32_t DynamicFpsController::getCurTargetFps() const
{
	return _curTargetFps;
}

uint32_t DynamicFpsController::getGameTargetFps() const
{
	return _gameTargetFps;
}

float DynamicFpsController::getCurTimeInOneFps() const
{
	return _curTimeInOneFps;
}

void DynamicFpsController::reset()
{
	frame_count = 0;

	time_now = std::chrono::high_resolution_clock::now();
	time_last = time_now;
	time_last_second = time_now;

	sleep_time = getCurTimeInOneFps() / 2;

	time_diff = getCurTimeInOneFps();
}
#include "Helper/Tools.h"
void DynamicFpsController::run()
{
	time_now = std::chrono::high_resolution_clock::now();

	Duration duration = time_now - time_last;
	time_diff = duration.count();
	time_last = time_now;
	frame_count++;

	int maxSleepTime = maxSleepAdjustTime;

	duration = time_now - time_last_second;
	if (duration.count() >= 1000.f)
	{
		time_last_second = time_now;
		float curTimeInOneFpsDuringSecond = duration.count() / frame_count;
		int delta = std::floor(curTimeInOneFpsDuringSecond - _curTimeInOneFps + 0.5f);

		sleep_time = std::clamp(sleep_time - delta, -maxSleepTime, maxSleepTime);

		frame_count = 0u;
	}

	if (time_diff - _curTimeInOneFps >= 0.f)
	{
		if (sleep_time > -maxSleepTime)
			sleep_time--;
	}
	else
	{
		if (sleep_time < maxSleepTime)
			sleep_time++;
	}

	if (sleep_time > 0)
		std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
}

float DynamicFpsController::getTimeDiffMS() const
{
	return time_diff > 0 ? time_diff : getCurTimeInOneFps();
}

DynamicFpsEstimate::DynamicFpsEstimate()
{
	_curTargetFps = 0;
	setCurTargetFps(60);
	reset();
}

uint32_t DynamicFpsEstimate::getCurTargetFps() const
{
	return _curTargetFps;
}

float DynamicFpsEstimate::getCurTimeInOneFps() const
{
	return _curTimeInOneFps;
}

void DynamicFpsEstimate::setCurTargetFps(uint32_t curTargetFps)
{
	if (curTargetFps == _curTargetFps)
		return;
	_curTargetFps = std::max(1u, curTargetFps);
	_curTimeInOneFps = 1000.f / _curTargetFps;
}

void DynamicFpsEstimate::reset()
{
	frame_count = 0;
	timeAcl = 0.f;
}

void DynamicFpsEstimate::run(float time_diff)
{
	frame_count++;
	timeAcl += time_diff;
	if (frame_count > 100)
	{
		float avgTimeDiff = timeAcl / (float)frame_count;
		setCurTargetFps(std::max(1u, uint32_t(1000.f / avgTimeDiff) + 1));
		frame_count = 0;
		timeAcl = 0.f;
	}

	if (time_diff - _curTimeInOneFps >= 0.1f)
	{
		setCurTargetFps(_curTargetFps - 1);
	}
	else
	{
		setCurTargetFps(_curTargetFps + 1);
	}
}

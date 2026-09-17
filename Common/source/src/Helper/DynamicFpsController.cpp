#include "Helper/DynamicFpsController.h"
#include <algorithm>
#include <thread>
#include <cmath>

DynamicFpsController::DynamicFpsController(int gameTargetFps)
{
	setGameTargetFps(gameTargetFps);
	setCurTargetFps(gameTargetFps);
}

void DynamicFpsController::setCurTargetFps(int curTargetFps)
{
	if (curTargetFps == _curTargetFps)
		return;
	_curTargetFps = std::max(1, curTargetFps);
	_curTimeInOneFps = 1000.f / _curTargetFps;
}

void DynamicFpsController::setGameTargetFps(int gameFps)
{
	_gameTargetFps = std::max(1, gameFps);
}

int DynamicFpsController::getCurTargetFps()
{
	return _curTargetFps;
}

int DynamicFpsController::getGameTargetFps()
{
	return _gameTargetFps;
}

float DynamicFpsController::getCurTimeInOneFps()
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

void DynamicFpsController::run()
{
	time_now = std::chrono::high_resolution_clock::now();

	Duration duration = time_now - time_last;
	time_diff = duration.count();
	time_last = time_now;
	frame_count++;

	duration = time_now - time_last_second;
	if (duration.count() >= 1000.f)
	{
		time_last_second = time_now;
		float curTimeInOneFpsDuringSecond = duration.count() / frame_count;
		int delta = std::floor(curTimeInOneFpsDuringSecond - _curTimeInOneFps + 0.5f);

		sleep_time = std::clamp(sleep_time - delta, -160, 160);

		// LOGINFO("Render , frame_count: {:#d} ", frame_count);
		frame_count = 0;
	}

	if (time_diff - _curTimeInOneFps >= 0.1f)
	{
		if (sleep_time > -160)
			sleep_time--;
	}
	else
	{
		if (sleep_time < 160)
			sleep_time++;
	}

	if (sleep_time > 0)
		std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
}

float DynamicFpsController::getTimeDiffMS()
{
	return time_diff > 0 ? time_diff : getCurTimeInOneFps();
}

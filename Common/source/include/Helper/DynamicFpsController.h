#pragma once

#include <chrono>

class DynamicFpsController
{
	using TimePoint = std::chrono::high_resolution_clock::time_point;
	using Duration = std::chrono::duration<float, std::milli>;

public:
	DynamicFpsController(int gameTargetFps);

	void setGameTargetFps(int gameTargetFps);
	int getGameTargetFps();

	void setCurTargetFps(int curTargetFps);
	int getCurTargetFps();
	float getCurTimeInOneFps();

	void reset();
	void run();

	float getTimeDiffMS();

private:
	float _curTimeInOneFps;
	int _curTargetFps;
	int _gameTargetFps;

private:
	int frame_count;
	TimePoint time_now;
	TimePoint time_last;
	TimePoint time_last_second;
	int sleep_time;
	float time_diff = 0;
};
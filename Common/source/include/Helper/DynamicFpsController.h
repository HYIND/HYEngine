#pragma once

#include <chrono>

class DynamicFpsController
{
	using TimePoint = std::chrono::high_resolution_clock::time_point;
	using Duration = std::chrono::duration<float, std::milli>;

public:
	DynamicFpsController(int gameTargetFps = 60);

	void setGameTargetFps(int gameTargetFps);
	int getGameTargetFps() const;

	void setCurTargetFps(int curTargetFps);
	int getCurTargetFps() const;
	float getCurTimeInOneFps() const;

	void reset();
	void run();

	float getTimeDiffMS() const;

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

class DynamicFpsEstimate
{
	using TimePoint = std::chrono::high_resolution_clock::time_point;
	using Duration = std::chrono::duration<float, std::milli>;

public:
	DynamicFpsEstimate();

	uint32_t getCurTargetFps() const;
	float getCurTimeInOneFps() const;

	void reset();
	void run(float time_diff);

private:
	void setCurTargetFps(uint32_t curTargetFps);

private:
	float _curTimeInOneFps;
	uint32_t _curTargetFps;

private:
	uint32_t frame_count;
	float timeAcl = 0.f;
};
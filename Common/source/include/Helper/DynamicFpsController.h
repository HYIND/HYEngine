#pragma once

#include <chrono>

class DynamicFpsController
{
	using TimePoint = std::chrono::high_resolution_clock::time_point;
	using Duration = std::chrono::duration<float, std::milli>;

public:
	DynamicFpsController(uint32_t gameTargetFps = 60);

	void setGameTargetFps(uint32_t gameTargetFps);
	uint32_t getGameTargetFps() const;

	void setCurTargetFps(uint32_t curTargetFps);
	uint32_t getCurTargetFps() const;
	float getCurTimeInOneFps() const;

	void reset();
	void run();

	float getTimeDiffMS() const;

private:
	float _curTimeInOneFps;
	uint32_t _curTargetFps;
	uint32_t _gameTargetFps;

private:
	uint32_t frame_count;
	TimePoint time_now;
	TimePoint time_last;
	TimePoint time_last_second;
	int sleep_time;
	float time_diff = 0;

	uint32_t maxSleepAdjustTime = 160u;
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
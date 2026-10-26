#pragma once

// SystemManager executes larger priorities first. These values are the
// Engine's scheduling landmarks; project/module systems choose positions
// relative to these anchors.
enum class EngineSystemPriority : int
{
	Lifetime = 10000,
	Input = 1000,
	Script = 450,
	Physics = 300,
	LaserBeam = 250,
	Particle = 200,
	Audio = 0,
	CameraFollow = -3000,
	Destroy = -8000,
	Animation = -9000,
	Render = -10000,
};

constexpr int ToSystemPriority(EngineSystemPriority priority) noexcept
{
	return static_cast<int>(priority);
}

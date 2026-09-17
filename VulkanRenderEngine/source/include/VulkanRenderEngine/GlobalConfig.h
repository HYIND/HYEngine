#pragma once

#include "vkstdafx.h"

namespace GlobalConfig
{
	inline constexpr uint32_t Mesh_Max_Bone_Influence = 2 * 4;

	inline constexpr uint32_t Mesh_BVH_Leaf_TriCount = 6;

	inline constexpr uint32_t RayTrace_Max_Recursive_Depth = 16;
	inline constexpr uint32_t RayTrace_Max_Bounce_limit = 3;
	inline constexpr uint32_t RayTrace_World_BVH_Leaf_MeshCount = 3;

	inline constexpr uint32_t SSTrace_Max_Bounce_limit = 4;

	inline constexpr float AutoExposure_MIN_EV = -6.0;
	inline constexpr float AutoExposure_MAX_EV = 12.0;
	inline constexpr float AutoExposure_EV_RANGE = AutoExposure_MAX_EV - AutoExposure_MIN_EV;

	inline bool RTCoreEnable = true;

	inline constexpr uint32_t MaxFramesInFlight = 2;
}
#pragma once

#include "glm/glm.hpp"

namespace GlobalConfig
{
	constexpr uint32_t Mesh_Max_Bone_Influence = 2 * 4;

	constexpr uint32_t Mesh_BVH_Leaf_TriCount = 6;

	constexpr uint32_t RayTrace_Max_Recursive_Depth = 16;
	constexpr uint32_t RayTrace_Max_Bounce_limit = 3;
	constexpr uint32_t RayTrace_World_BVH_Leaf_MeshCount = 3;

	constexpr uint32_t SSTrace_Max_Bounce_limit = 4;

	constexpr float AutoExposure_MIN_EV = -6.0;
	constexpr float AutoExposure_MAX_EV = 12.0;
	constexpr float AutoExposure_EV_RANGE = AutoExposure_MAX_EV - AutoExposure_MIN_EV;

}
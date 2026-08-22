#pragma once

#include "glm/glm.hpp"

namespace OpenGLRenderConfig
{
	constexpr int Mesh_Max_Bone_Influence = 2 * 4; 

	constexpr int Mesh_BVH_Leaf_TriCount = 6;

	constexpr int RayTrace_Max_Recursive_Depth = 16;
	constexpr int RayTrace_Max_Bounce_limit = 2;
	constexpr int RayTrace_World_BVH_Leaf_MeshCount = 3;

	constexpr int SSTrace_Max_Bounce_limit = 2;

	constexpr float AutoExposure_MIN_EV = -6.0;
	constexpr float AutoExposure_MAX_EV = 12.0;
	constexpr float AutoExposure_EV_RANGE = AutoExposure_MAX_EV - AutoExposure_MIN_EV;

}
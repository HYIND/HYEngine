#pragma once

#include "glm/glm.hpp"

namespace OpenGLRenderConfig
{
	constexpr int Mesh_Max_Bone_Influence = 2 * 4; 

	constexpr int Mesh_BVH_Leaf_TriCount = 6;

	constexpr int RayTrace_Max_Recursive_Depth = 16;
	constexpr int RayTrace_Max_Bounce_limit = 2;
	constexpr int RayTrace_World_BVH_Leaf_MeshCount = 3;


	constexpr int SSR_Max_Step = 256;
	constexpr int SSR_Max_Bounce_limit = 2;
	constexpr int SSR_BlurKernelSize = 3;
	constexpr float SSR_BlurGaussSigma = 0.6;
	constexpr float SSR_BlurRadius = 0.5;
	constexpr float SSR_BlurDepthWeight = 10.0;


	constexpr int SSGI_Max_Step = 25;
	constexpr int SSGI_NUM_SAMPLES = 6;
	constexpr float SSGI_Sample_Indirect_Clamp_Value = 5.0;
	constexpr float SSGI_GIIntensity = 8;
	constexpr float SSGI_AOIntensity = 0.6;
	constexpr int SSGI_BlurKernelSize = 3;
	constexpr float SSGI_BlurGaussSigma = 1.8;
	constexpr float SSGI_BlurRadius = 1;
	constexpr float SSGI_BlurDepthWeight = 10.0;


	constexpr float AutoExposure_MIN_EV = -6.0;
	constexpr float AutoExposure_MAX_EV = 12.0;
	constexpr float AutoExposure_EV_RANGE = AutoExposure_MAX_EV - AutoExposure_MIN_EV;

}
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

	inline constexpr uint32_t MaxFramesInFlight = 2;

	// DDGI 容量：启动时按它分配探针图集（运行中不变）。
	// 每探针常驻显存约 3.4 KB（辐照度 R11G11B10 时），65536 个约 220 MB。
	inline uint32_t DDGIProbeCapacity = 65536;
	// DDGI 每帧最多更新的探针数，rayData 按它分配；运行时预算（DDGIParams::updateBudget）不能超过它
	inline uint32_t DDGIUpdateBudget = 65536;

	inline bool RTCoreEnable = false;
	inline bool RTCoreReorderEnable = false;

	inline bool VK_DESCRIPTOR_HEAP_Enable = false;

	inline uint32_t Global_WorkSize_X = 8;
	inline uint32_t Global_WorkSize_Y = 8;
}
#pragma once

#include "OpenGLRenderConfig.h"

struct RenderOption
{
	struct RayTraceGeneralParams {
		float maxDistance = 100.f;					// 反射最大计算距离
		float maxCacheClearDistance = 150.f;		// mesh缓存清除距离
	} rayTraceGeneralParams;

	struct RayTraceReflectParams {
		float tMin = 0.01;
		float tMax = 300.f;
		int maxBounceLimit = 2;
		bool useDenoised = false;
	} rayTraceReflectParams;

	struct RayTraceGIParams {
		float tMin = 0.01;
		float tMax = 300.f;
		int maxBounceLimit = 2;

		int NumSamples = 1;
		float GIIntensity = 1.0;
		float AOIntensity = 0.6;
		float DistanceFactor = 0.05;

		int BlurKernelSize = 3;
		float BlurGaussSigma = 1.8;
		float BlurRadius = 1;
		float BlurDepthWeight = 10.0;
	} rayTraceGIParams;

	struct SSRTraceParams {
		float tMin = 0.01;
		float tMax = 300.f;
		int maxBounceLimit = 1;

		int RayMarchingMaxStep = 256;

		int BlurKernelSize = 3;
		float BlurGaussSigma = 0.6;
		float BlurRadius = 0.5;
		float BlurDepthWeight = 10.0;

	} ssrTraceParams;

	struct SSGITraceParams {
		float tMin = 0.01;
		float tMax = 100.f;
		int maxBounceLimit = 1;

		int RayMarchingMaxStep = 25;
		int NumSamples = 6;
		float Sample_Indirect_Clamp_Value = 5.0;
		float GIIntensity = 8;
		float AOIntensity = 0.6;
		float DistanceFactor = 0.05;

		int BlurKernelSize = 3;
		float BlurGaussSigma = 1.8;
		float BlurRadius = 1;
		float BlurDepthWeight = 10.0;

	} ssgiTraceParams;

	struct DepthFogParams {
		glm::vec3 fogColor = glm::vec3(0.25, 0.3, 0.6);
		float fogHeight = -45.f;
		float fogDistanceFalloff = 0.02;
		float fogHeightFalloff = 0.01;
	} depthFogParams;

	struct PostProcessParams {
		float EV100 = 0.0f;
		float gamma = 2.2f;
	} postProcessParams;

	// 渲染开关
	struct PostProcessFlags {
		bool bloomOn = true;
		bool gammaOn = true;
		bool lightDrawOn = true;
		bool drawTransparent = true;
		bool ssrOn = false;
		bool ssgiOn = false;
		bool skyboxOn = true;
		bool depthFogOn = true;
		bool rayTraceReflectOn = false;
		bool rayTraceGIOn = false;
		bool autoExposureOn = false;
		bool calculateOcclusionCulling = false;
	} flags;
};
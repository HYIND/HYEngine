#pragma once

#include "OpenGLRenderConfig.h"

struct RenderOption
{
	struct RayTraceParams {
		float tMin = 0.01;
		float tMax = 300.f;
		int maxBounceLimit = 1;
		bool useTAA = false;
		bool useDenoised = false;
		bool useGbuffer = true;
	} rayTraceParams;

	struct SSRTraceParams {
		float tMin = 0.01;
		float tMax = 300.f;
		int maxBounceLimit = 1;
	} ssrTraceParams;

	struct SSGITraceParams {
		float tMin = 0.01;
		float tMax = 100.f;
		int maxBounceLimit = 1;
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
		bool ssgiOn = true;
		bool skyboxOn = true;
		bool depthFogOn = true;
		bool rayTraceOn = false;
		bool autoExposureOn = false;
		bool calculateOcclusionCulling = true;
	} flags;
};
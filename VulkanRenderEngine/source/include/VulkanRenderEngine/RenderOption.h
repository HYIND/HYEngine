#pragma once

struct RenderOption
{
	struct RayTraceGeneralParams {
		float maxDistance = 200.f;					// 反射最大计算距离
		float maxCacheClearDistance = 250.f;		// mesh缓存清除距离
	} rayTraceGeneralParams;

	struct RayTraceReflectParams {
		float tMin = 0.01;
		float tMax = 300.f;
		uint32_t maxBounceLimit = 2;

		uint32_t NumSamples = 2;

		float normalFactor = 128.0;
		float depthFactor = 1.0;
		float luminanceFactor = 0.033;

		uint32_t maxAccumulateCount = 50;
		uint32_t filterCount = 5;
	} rayTraceReflectParams;

	struct RayTraceGIParams {
		float tMin = 0.01;
		float tMax = 300.f;
		uint32_t maxBounceLimit = 2;

		uint32_t NumSamples = 3;
		float GIIntensity = 1.0;

		float normalFactor = 128.0;
		float depthFactor = 1.0;
		float luminanceFactor = 0.033;

		uint32_t maxAccumulateCount = 50;
		uint32_t filterCount = 5;
	} rayTraceGIParams;

	struct SSRTraceParams {
		float tMin = 0.01;
		float tMax = 300.f;
		uint32_t maxBounceLimit = 2;

		uint32_t RayMarchingMaxStep = 40;
		uint32_t NumSamples = 6;
		float Sample_Indirect_Clamp_Value = 2.0;
		float DistanceFactor = 0.02;

		float normalFactor = 128.0;
		float depthFactor = 1.0;
		float luminanceFactor = 0.033;

		uint32_t maxAccumulateCount = 32;
		uint32_t filterCount = 5;
	} ssrTraceParams;

	struct SSGITraceParams {
		float tMin = 0.01;
		float tMax = 300.f;
		uint32_t maxBounceLimit = 2;

		uint32_t RayMarchingMaxStep = 30;
		uint32_t NumSamples = 6;
		float Sample_Indirect_Clamp_Value = 2.0;
		float GIIntensity = 1.0;
		float AOIntensity = 0.8;
		float DistanceFactor = 0.05;

		float normalFactor = 128.0;
		float depthFactor = 1.0;
		float luminanceFactor = 0.033;

		uint32_t maxAccumulateCount = 32;
		uint32_t filterCount = 5;
	} ssgiTraceParams;

	struct DepthFogParams {
		glm::vec3 fogColor = glm::vec3(0.25, 0.3, 0.6);
		float fogHeight = -45.f;
		float fogDistanceFalloff = 0.02;
		float fogHeightFalloff = 0.01;
	} depthFogParams;

	struct AtmosphereParams {
		glm::vec3 CubeCapturePosition = glm::vec3(0, 500, 0);
		float PlanetRadius = 6370 * 1e3;
		float AtmosphereHeight = 100 * 1e3;
		float RayleighScatteringScalarHeight = 8 * 1e3;
		float MieScatteringScalarHeight = 1.2 * 1e3;
		float MieAnisotropy = 0.8;
		float OzoneLevelCenterHeight = 25 * 1e3;
		float OzoneLevelWidth = 15 * 1e3;
		uint32_t ScatterPathSampleCount = 50;
		uint32_t TransmittanceSampleCount = 100;
	} atmosphereParams;

	struct SSAOParams {
		float radius = 20.0;
		float bias = 0.05;
	} ssaoParams;

	struct PostProcessParams {
		float EV100 = 0.0f;
		float gamma = 2.2f;
	} postProcessParams;

	// 渲染开关
	struct PostProcessFlags {
		bool bloomOn = true;
		bool gammaOn = true;
		bool lightDrawOn = false;
		bool drawTransparent = true;
		bool ssrOn = false;
		bool ssgiOn = false;
		bool skyboxOn = false;
		bool depthFogOn = false;
		bool atmosphereOn = true;
		bool rayTraceReflectOn = false;
		bool rayTraceGIOn = false;
		bool autoExposureOn = false;
		bool calculateOcclusionCulling = true;
	} flags;
};
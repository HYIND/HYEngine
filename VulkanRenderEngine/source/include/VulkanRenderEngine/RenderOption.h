#pragma once

struct Scene3DRenderOption
{
	enum class TransparentMode :uint32_t {
		Sorted = 0,
		OIT = 1
	};

	struct RayTraceGeneralParams {
		float maxDistance = 750;				// 反射最大计算距离
		float maxCacheClearDistance = 1000;		// mesh缓存清除距离
	} rayTraceGeneralParams;

	struct RayTraceReflectParams {
		float tMin = 0.01;
		float tMax = 1000.0;
		uint32_t maxBounceLimit = 2;

		uint32_t NumSamples = 2;

		float normalFactor = 128.0;
		float depthFactor = 1.0;
		float luminanceFactor = 0.04;

		float depthThreshold = 0.02;
		float positiondistThreshold = 0.05;
		float normalThreshold = 0.988;
		float motionWeightScale = 0.05;

		uint32_t maxAccumulateCount = 64;
		uint32_t filterCount = 5;
	} rayTraceReflectParams;

	struct RayTraceGIParams {
		float tMin = 0.01;
		float tMax = 1000.0;
		uint32_t maxBounceLimit = 2;

		uint32_t NumSamples = 3;
		float GIIntensity = 1.0;

		float normalFactor = 128.0;
		float depthFactor = 0.8;
		float luminanceFactor = 0.04;

		float depthThreshold = 0.02;
		float positiondistThreshold = 0.05;
		float normalThreshold = 0.988;
		float motionWeightScale = 0.05;

		uint32_t maxAccumulateCount = 64;
		uint32_t filterCount = 5;
	} rayTraceGIParams;

	struct SSRTraceParams {
		float tMin = 0.01;
		float tMax = 300.f;
		uint32_t maxBounceLimit = 2;

		uint32_t RayMarchingMaxStep = 40;
		uint32_t NumSamples = 6;
		float DistanceFactor = 0.02;

		float normalFactor = 128.0;
		float depthFactor = 1.0;
		float luminanceFactor = 0.04;

		float depthThreshold = 0.02;
		float positiondistThreshold = 0.05;
		float normalThreshold = 0.988;
		float motionWeightScale = 0.05;

		uint32_t maxAccumulateCount = 48;
		uint32_t filterCount = 5;
	} ssrTraceParams;

	struct SSGITraceParams {
		float tMin = 0.01;
		float tMax = 300.f;
		uint32_t maxBounceLimit = 2;

		uint32_t RayMarchingMaxStep = 30;
		uint32_t NumSamples = 6;
		float GIIntensity = 1.0;
		float AOIntensity = 0.8;
		float DistanceFactor = 0.05;

		float normalFactor = 128.0;
		float depthFactor = 1.0;
		float luminanceFactor = 0.04;

		float depthThreshold = 0.02;
		float positiondistThreshold = 0.05;
		float normalThreshold = 0.988;
		float motionWeightScale = 0.05;

		uint32_t maxAccumulateCount = 48;
		uint32_t filterCount = 5;
	} ssgiTraceParams;

	struct alignas(16) ExponentialHeightFogParams {
		glm::vec3 fogAlbedo = glm::vec3(1.0, 1.0, 1.0);
		float globalDensity = 0.1;			// 全局雾浓度
		float fogStartHeight = 5.0;			// 雾起始高度
		float fogStartDistance = 5.0;		// 雾起始距离
		float fogHeightFalloff = 5.0;		// 高度衰减系数
		bool volumeFogOn = false;
		float fogMaxOpacity = 1.0f;			// 非体积高度雾的最大不透明度
		uint32_t VolumeLightScatterPathSampleCount = 20; // 体积光步进采样数
	} exponentialHeightFogParams;

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

	// 体积云参数。长度单位统一是米，和 AtmosphereParams 一致
	struct VolumetricCloudParams {
		glm::vec3 cloudAlbedo = glm::vec3(1.0f);						// 云的反照率
		float cloudBottomHeight = 1500.0f;								// 云底高度（米）
		glm::vec3 cloudAmbientColor = glm::vec3(0.35f, 0.42f, 0.55f);	// 环境光，天空散射的兜底
		float cloudTopHeight = 4000.0f;									// 云顶高度（米）

		float coverage = 0.45f;				// 覆盖率 0~1，越大云越多
		float density = 0.03f;				// 密度缩放（1/米），密度为 1 时的消光系数
		float shapeScale = 1.0f / 20000.0f;	// 世界坐标 -> 形状噪声坐标（1/米）
		float detailScale = 1.0f / 2000.0f;	// 细节噪声的缩放（1/米）
		float detailStrength = 0.35f;		// 细节侵蚀强度
		float phaseG = 0.8f;				// 双瓣 HG 的前向 g
		float phaseBlend = 0.5f;			// 前向瓣 / 后向瓣的混合比
		float powder = 1.0f;				// powder 效应强度
		float maxTraceDistance = 30000.0f;	// 视线追踪最远距离（米）
		float erosionDepth = 400.0f;		// 云顶、云底的软收边深度（米）

		uint32_t viewStepCount = 25;		// 视线步数
		uint32_t lightStepCount = 6;		// 光步步数
	} volumetricCloudParams;

	// 半透明渲染方式
	struct TransparentParams {
		TransparentMode mode = TransparentMode::OIT;
	} transparentParams;

	// 渲染开关
	struct PostProcessFlags {
		bool bloomOn = true;
		bool gammaOn = true;
		bool lightDrawOn = false;
		bool drawTransparent = true;
		bool ssrOn = false;
		bool ssgiOn = false;
		bool skyboxOn = true;
		bool exponentialHeightFogOn = false;
		bool atmosphereOn = true;
		bool volumetricCloudOn = true;
		bool rayTraceReflectOn = false;
		bool rayTraceGIOn = false;
		bool autoExposureOn = false;
		bool calculateOcclusionCulling = true;
	} flags;
};

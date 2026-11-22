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
		float motionWeightScale = 0.01;

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
		float motionWeightScale = 0.01;

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
		float motionWeightScale = 0.01;

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
		float motionWeightScale = 0.01;

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

		uint32_t viewStepCount = 32;		// 视线步数
		uint32_t lightStepCount = 6;		// 光步步数
	} volumetricCloudParams;

	// 半透明渲染方式
	struct TransparentParams {
		TransparentMode mode = TransparentMode::OIT;
	} transparentParams;

	// DDGI 探针全局光照，距离单位为米。
	// 对外用体积中心 center 描述，渲染器内部以最小角 origin 为基准（origin = snap(center - counts * spacing / 2)）。
	struct DDGIParams {
		// 布置方式，优先级 autoFitVolume > followCamera > 手动 center：
		//   autoFitVolume  按场景包围盒 + fitPadding 自动拟合，忽略 center / counts
		//   followCamera   counts 作为滑动窗口跟随相机，整格平移时只重置新进入的探针层
		bool autoFitVolume = true;
		bool followCamera = false;
		float fitPadding = 1.0f;						// 自动拟合的外扩边距，实际至少一格 spacing

		glm::vec3 center = glm::vec3(0.0f);				// 手动模式的体积中心
		// 探针间距，画质的首要因素。建议 0.75~1.25m；探针数 ∝ 1/spacing³，用 updateSliceCount 分摊开销
		glm::vec3 spacing = glm::vec3(2.0f);
		glm::uvec3 counts = glm::uvec3(32, 32, 16);		// 每轴探针数，总数上限 DDGIAtlasLayout::MaxProbeCount

		// 采样偏移：沿法线 normalBias、朝观察者 viewBias（Apply 朝相机，多次反弹朝探针），
		// 抑制自遮挡与掠射角漏光。建议均为 spacing 的 0.1~0.3 倍
		float normalBias = 0.5f;
		float viewBias = 0.5f;
		float maxRayDistance = 24.0f;					// 探针光线最远距离，未命中视为看到天空
		// 背面权重指数：w = ((N·dirToProbe + 1) / 2)^k + 0.2。调大漏光更少但格子感更强
		float backfaceThreshold = 2.0f;
		float skyIntensity = 1.0f;						// 未命中光线的天空强度，0 = 没有天空光
		float energy = 1.0f;							// 间接光整体强度

		// 每帧更新 1/N 的探针，1 = 全量
		uint32_t updateSliceCount = 3;
		// 时域混合：new = mix(history, current, 1 - hysteresis)。调大更稳但动态光拖尾更长，
		// 等效收敛时间约 sliceCount / (1 - hysteresis) 帧。重置后前几次更新按 1/n 平均，几帧内收敛
		float hysteresis = 0.92f;
		// 光照突变阈值：探针平均亮度的相对变化超过它（且超出跨帧统计的噪声）时，
		// 本次更新该探针的 hysteresis 降低 0.75，开关灯时快速跟上。调小响应更积极，调大更稳；0 = 关闭
		float lightChangeThreshold = 0.0f;
	} ddgiParams;

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
		bool ddgiOn = false;
		bool autoExposureOn = false;
		bool calculateOcclusionCulling = true;
	} flags;
};

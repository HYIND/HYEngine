#pragma once

struct Scene3DRenderOption
{
	enum class TransparentMode :uint32_t {
		Sorted = 0,
		OIT = 1
	};

	enum class ReflectionFogVolumeMode : uint32_t {
		Off = 0,
		DirectionalOnly = 1,
		AllLights = 2
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
		float luminanceFactor = 4.0;		// SVGF 的 σ_l：方差标准差的倍数（不是绝对亮度），见 Filter/Atrous-BilateralFilter.comp

		float depthThreshold = 0.02;
		float positiondistThreshold = 0.05;
		float normalThreshold = 0.988;
		float motionWeightScale = 0.0;
		float varianceClipGamma = 2.0;		// 时域历史裁剪：本帧快速历史（6 帧）3×3 邻域均值 ± γ·σ，超出时同时缩短历史（anti-lag）；<= 0 关闭。拖影明显调小（1 ~ 1.5），噪点多调大（3）

		uint32_t maxAccumulateCount = 64;
		uint32_t filterCount = 5;
		ReflectionFogVolumeMode fogVolumeMode = ReflectionFogVolumeMode::DirectionalOnly;
	} rayTraceReflectParams;

	struct RayTraceGIParams {
		float tMin = 0.01;
		float tMax = 1000.0;
		uint32_t maxBounceLimit = 2;

		uint32_t NumSamples = 3;
		float GIIntensity = 1.0;

		float normalFactor = 128.0;
		float depthFactor = 0.8;
		float luminanceFactor = 4.0;		// SVGF 的 σ_l：方差标准差的倍数（不是绝对亮度），见 Filter/Atrous-BilateralFilter.comp

		float depthThreshold = 0.02;
		float positiondistThreshold = 0.05;
		float normalThreshold = 0.988;
		float motionWeightScale = 0.0;
		float varianceClipGamma = 2.0;		// 时域历史裁剪：本帧快速历史（6 帧）3×3 邻域均值 ± γ·σ，超出时同时缩短历史（anti-lag）；<= 0 关闭。拖影明显调小（1 ~ 1.5），噪点多调大（3）

		uint32_t maxAccumulateCount = 64;
		uint32_t filterCount = 5;
		ReflectionFogVolumeMode fogVolumeMode = ReflectionFogVolumeMode::DirectionalOnly;
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
		float luminanceFactor = 4.0;		// SVGF 的 σ_l：方差标准差的倍数（不是绝对亮度），见 Filter/Atrous-BilateralFilter.comp

		float depthThreshold = 0.02;
		float positiondistThreshold = 0.05;
		float normalThreshold = 0.988;
		float motionWeightScale = 0.0;
		float varianceClipGamma = 2.0;		// 时域历史裁剪：本帧快速历史（6 帧）3×3 邻域均值 ± γ·σ，超出时同时缩短历史（anti-lag）；<= 0 关闭。拖影明显调小（1 ~ 1.5），噪点多调大（3）

		uint32_t maxAccumulateCount = 48;
		uint32_t filterCount = 5;
		ReflectionFogVolumeMode fogVolumeMode = ReflectionFogVolumeMode::DirectionalOnly;
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
		float luminanceFactor = 4.0;		// SVGF 的 σ_l：方差标准差的倍数（不是绝对亮度），见 Filter/Atrous-BilateralFilter.comp

		float depthThreshold = 0.02;
		float positiondistThreshold = 0.05;
		float normalThreshold = 0.988;
		float motionWeightScale = 0.0;
		float varianceClipGamma = 2.0;		// 时域历史裁剪：本帧快速历史（6 帧）3×3 邻域均值 ± γ·σ，超出时同时缩短历史（anti-lag）；<= 0 关闭。拖影明显调小（1 ~ 1.5），噪点多调大（3）

		uint32_t maxAccumulateCount = 48;
		uint32_t filterCount = 5;
		ReflectionFogVolumeMode fogVolumeMode = ReflectionFogVolumeMode::DirectionalOnly;
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
		glm::vec3 CubeCapturePosition = glm::vec3(0, 1000, 0);
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
		float density = 0.015f;				// 密度缩放（1/米），密度为 1 时的消光系数
		float shapeScale = 1.0f / 20000.0f;	// 世界坐标 -> 形状噪声坐标（1/米）
		float detailScale = 1.0f / 2000.0f;	// 细节噪声的缩放（1/米）
		float detailStrength = 0.35f;		// 细节侵蚀强度
		float phaseG = 0.2f;				// 双瓣 HG 的前向 g
		float multiScatterContribution = 0.8f;	// 每增加一阶的散射系数衰减 a
		float multiScatterOcclusion = 0.85f;	// 每增加一阶的消光系数衰减 b（必须 >= a）
		float multiScatterEccentricity = 0.5f;	// 每阶 HG g 的衰减 c，高阶散射更接近各向同性
		float powder = 0.4f;				// powder 效应强度
		float maxTraceDistance = 30000.0f;	// 视线追踪最远距离（米）
		float erosionDepth = 400.0f;		// 云顶、云底的软收边深度（米）

		uint32_t viewStepCount = 32;		// 视线步数
		uint32_t lightStepCount = 6;		// 光步步数
		uint32_t multiScatterOctaves = 4;	// 总散射阶数，1=仅单次散射
		float skyFillStrength = 0.6f;		// 天空补光强度，单独作用一次
		float skyFillDesaturation = 0.9f;	// 天空补光去饱和度：0=原色，1=灰度
		float skyFillBottomOcclusion = 0.4f;	// 云底补光比例；云顶逐渐恢复至 1
	} volumetricCloudParams;

	// 半透明渲染方式
	struct TransparentParams {
		TransparentMode mode = TransparentMode::OIT;
	} transparentParams;

	// DDGI 探针全局光照，距离单位为米。
	// 探针来源有两种，可以叠加：
	//   1) 跟随相机的级联（cascadeEnable）：以相机为中心的 cascadeLevels 级体积，间距逐级翻倍；
	//   2) 场景里的 DDGIVolumeComponent：手动摆放的体积，优先级高于所有级联。
	// 下面的着色 / 时域参数对两者都生效。
	struct DDGIParams {
		bool cascadeEnable = true;
		uint32_t cascadeLevels = 4;							// 1~4
		glm::vec3 cascadeBaseSpacing = glm::vec3(1.0f);		// 第 0 级间距，之后每级翻倍
		glm::uvec3 cascadeCounts = glm::uvec3(24, 12, 24);	// 每级的探针数
		float cascadeFadeCells = 2.0f;						// 级与级之间的过渡宽度（格）
		bool cascadeAutoRelocate = true;					// 级联探针自动避开几何

		// 采样偏移：沿法线 normalBias、朝观察者 viewBias（Apply 朝相机，多次反弹朝探针），
		// 抑制自遮挡与掠射角漏光。单位是「所在体积最小间距的倍数」，各级级联 / 手动体积各自缩放，
		// 建议 0.1~0.3。两者之和不要超过 0.5：覆盖剔除的余量（BuildCoveredRanges / ddgiProbeCovered）
		// 只给采样偏移留了 0.5 格
		float normalBias = 0.1f;
		float viewBias = 0.10f;
		float maxRayDistance = 15.0f;					// 探针光线最远距离，未命中视为看到天空
		// 背面权重指数：w = ((N·dirToProbe + 1) / 2)^k + 0.2。调大漏光更少但格子感更强
		float backfaceThreshold = 2.0f;
		float skyIntensity = 0.2f;						// 未命中光线的天空强度，0 = 没有天空光
		float energy = 1.0f;							// 间接光整体强度

		// 每帧最多更新的探针数（不超过 GlobalConfig::DDGIUpdateBudget）。
		// 预算按距离分给各体积：近处更新勤、远处慢；新出现的探针优先
		uint32_t updateBudget = 8192;
		// 时域混合：new = mix(history, current, 1 - hysteresis)。调大更稳但动态光拖尾更长。
		// 重置后前几次更新按 1/n 平均，几帧内收敛
		float hysteresis = 0.92f;
		// 光照突变阈值：探针平均亮度的相对变化超过它（且超出跨帧统计的噪声）时，
		// 本次更新该探针的 hysteresis 降低 0.75。0 = 关闭
		float lightChangeThreshold = 0.0f;
	} ddgiParams;

	// 渲染开关
	struct PostProcessFlags {
		bool bloomOn = true;
		bool gammaOn = true;
		bool lightDrawOn = false;
		bool ddgiProbeDrawOn = false;		// DDGI 探针可视化（编辑器调试）
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

	// 降噪调试（运行时调试状态，不序列化：不在 EngineRuntime 的 JSON 字段列表里）。
	// 把某个 Pass 时域累积的内部状态画成热力图，并在间接光最终合成时替换场景颜色显示（见 Temporal/TemporalAccumulate.comp）。
	// 同一时刻只调试一个 Pass；RTR / RTGI 的软件光追版本与硬件版本共用同一个目标。
	enum class DenoiseDebugTarget : uint32_t {
		None = 0,
		RayTraceReflect,
		RayTraceGI,
		SSR,
		SSGI,
	};
	enum class DenoiseDebugMode : uint32_t {
		Off = 0,
		HistoryLength,		// 混合后的历史长度：黑 = 没有历史，蓝 → 红 = 越来越长（到 maxAccumulateCount）
		RejectReason,		// 历史被拒的原因（2×2 各点按双线性权重叠加）：红 = 法线，绿 = 平面距离，蓝 = 世界距离，品红 = 历史是天空，白 = 出屏幕，灰 = 通过
		ClipAmount,			// 方差裁剪把历史拉动了多少：暗灰 = 在盒子里没动，蓝 → 红 = 超出盒子 1 ~ 4 倍半径
		HistoryWeight,		// 2×2 重投影里通过检测的双线性权重之和：黑 = 0（没有历史），红 = 1（全部通过）
	};
	struct DenoiseDebugParams {
		DenoiseDebugTarget target = DenoiseDebugTarget::None;
		DenoiseDebugMode mode = DenoiseDebugMode::Off;

		bool Active() const { return target != DenoiseDebugTarget::None && mode != DenoiseDebugMode::Off; }
		// 传给 TemporalAccumulate 的调试模式：不是这个 Pass 时为 0
		uint32_t ModeFor(DenoiseDebugTarget t) const { return (Active() && target == t) ? uint32_t(mode) : 0u; }
	} denoiseDebug;

	// 阴影调试（运行时调试状态，不序列化）。只对编辑器里选中的、启用了的方向光生效（shadowDebugEntity），
	// 在 LightingPass 里替换直接光颜色；没有选中方向光时正常渲染。
	// 品红斜纹 = 接收点投影到了所选级联范围外；黑白斜纹 = 开了投影但一个级联都没分到；暗灰 = 超出阴影距离
	enum class ShadowDebugMode : uint32_t {
		Off = 0,
		Cascade,		// 按级联着色：红 0 / 绿 1 / 蓝 2 / 黄 3，阴影处变暗，渐隐段加斜纹
		ShadowFactor,	// 只显示阴影结果：白 = 受光，黑 = 阴影
		ShadowMapContent,	// 阴影图在接收点处存了什么：青 = 空（没有物体画进来），红 = 有更近的遮挡物，绿 = 只有接收面自己
	};
	ShadowDebugMode shadowDebugMode = ShadowDebugMode::Off;
	uint32_t shadowDebugEntity = UINT32_MAX;	// 要调试的方向光所属实体（编辑器每帧按选中实体设置），UINT32_MAX = 没有

	// 降噪调试目标对应的 Pass 开着没有（RTR / RTGI 的软件、硬件版本共用同一个开关）
	bool IsDenoiseDebugTargetOn(DenoiseDebugTarget t) const
	{
		switch (t)
		{
		case DenoiseDebugTarget::RayTraceReflect: return flags.rayTraceReflectOn;
		case DenoiseDebugTarget::RayTraceGI: return flags.rayTraceGIOn;
		case DenoiseDebugTarget::SSR: return flags.ssrOn;
		case DenoiseDebugTarget::SSGI: return flags.ssgiOn;
		default: return false;
		}
	}
	// 降噪调试真正生效：选了模式，并且被调试的 Pass 开着。
	// Pass 没开时它不执行、所在分支的输出被清零，拿它替换场景颜色只会是一片黑，所以这时按正常画面显示
	bool DenoiseDebugEffective() const
	{
		return denoiseDebug.Active() && IsDenoiseDebugTargetOn(denoiseDebug.target);
	}
};

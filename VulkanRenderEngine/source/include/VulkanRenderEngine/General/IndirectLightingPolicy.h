#pragma once

#include <memory>
#include "VulkanRenderEngine/General/RenderState.h"
#include "VulkanRenderEngine/GlobalConfig.h"

// 间接光分工：按「漫反射 / 镜面」两个维度组织，另有一路给同时覆盖两个维度的路径追踪方案。
//
//   Pass           输出到                 内容
//   DDGI           漫反射合成             纯漫反射（体积没覆盖到的权重回退到 IBL 漫反射）
//   SSR / RTR      镜面合成               纯镜面（不追踪的像素回退到 IBL 镜面）
//   SSGI / RTGI    路径追踪合成           路径追踪（漫反射 + 镜面两个波瓣都有）
//   LightingPass   场景颜色               直接光 + 自发光 + 没有 Pass 负责的那一项 IBL
//
// LightingPass 补 IBL 的规则：
//   SSR / RTR 开启   → 不补镜面
//   DDGI 开启        → 不补漫反射
//   SSGI / RTGI 开启 → 两项都不补（路径追踪已经包含漫反射和镜面）
//
// 渲染图（见 VKRenderer3D::InitSceneRenderGraph）：
//   indirectDiffuseCombinNode     = DDGI          → indirectDiffuse_Output
//   indirectSpecularCombinNode    = RTR + SSR     → indirectSpecular_Output
//   indirectPathTracingCombinNode = RTGI + SSGI   → indirectPathTracing_Output
//   combinIndirectLightingNode    = 场景颜色 + 漫反射 + 镜面 + 路径追踪
// 漫反射 / 镜面两路只收纯漫反射 / 纯镜面的方案。同一路里多个 Pass 同时开启时不做互斥，直接相加。
// indirect*_Output 是相机视角下的最终合成结果，只给 combinIndirectLightingNode 用，不被前面的 Pass 回读。
// 反射（SSR / RTR）路径命中点的漫反射间接光按命中点的世界位置 / 法线直接采样 DDGI（没开或没覆盖到时用 IBL）。
struct IndirectLightingPolicy
{
	bool reflectOn = false;				// SSR / RTR：镜面
	bool ddgiOn = false;				// DDGI：漫反射（只在有硬件光追时执行）
	bool pathTraceOn = false;			// SSGI / RTGI：路径追踪，漫反射 + 镜面

	bool lightingIBLDiffuse = false;	// LightingPass 需要补 IBL 漫反射
	bool lightingIBLSpecular = false;	// LightingPass 需要补 IBL 镜面
};

inline IndirectLightingPolicy GetIndirectLightingPolicy(const RenderState& state)
{
	const auto& flags = state.option.flags;

	IndirectLightingPolicy policy;
	policy.reflectOn = flags.ssrOn || flags.rayTraceReflectOn;
	policy.ddgiOn = flags.ddgiOn && GlobalConfig::RTCoreEnable;	// 同 DDGI 各 Pass 的 ShouldExecute
	policy.pathTraceOn = flags.ssgiOn || flags.rayTraceGIOn;

	policy.lightingIBLDiffuse = !policy.ddgiOn && !policy.pathTraceOn;
	policy.lightingIBLSpecular = !policy.reflectOn && !policy.pathTraceOn;
	return policy;
}

// IBL 资源：由 IBLPreCalculatePass 统一生产，来源（云 / 大气 / 天空盒）对消费者透明。
// LightingPass、DDGIApply 与三组反射 Pass 都取这一份，保证不同维度的 IBL 回退一致。
// skyCube 是云 + 背景合成后的最终天空 cube，反射未命中 / GI 天空回退用它采样，保证和 IBL 一致地显示云。
// IBLPreCalculatePass 在 Execute 里写入 state.iblParams，因此只能在 Execute 里调用（不能在 FrameBegin）。
struct IBLResources
{
	std::shared_ptr<TextureCube> diffuse;		// 辐照度 E
	std::shared_ptr<TextureCube> prefilter;
	std::shared_ptr<Texture2D> brdfLUT;
	std::shared_ptr<TextureCube> skyCube;		// 最终天空 cube（含云），反射 / GI 天空回退用

	bool IBLReady() const
	{
		return diffuse && !diffuse->IsEmpty()
			&& prefilter && !prefilter->IsEmpty()
			&& brdfLUT && !brdfLUT->IsEmpty();
	}
};

inline IBLResources SelectIBLResources(const RenderState& state)
{
	const auto& p = state.iblParams;
	IBLResources res;
	res.diffuse = p.diffuse;
	res.prefilter = p.prefilter;
	res.brdfLUT = p.brdfLUT;
	res.skyCube = p.finalSkyCube;
	return res;
}

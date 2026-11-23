#include "RenderOptionPanels.h"
#include "EditorApp.h"
#include "PropertiesHelper.h"
#include "DDGIEditor.h"
#include "VulkanRenderEngine/RenderOption.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace
{
	using Flags = Scene3DRenderOption::PostProcessFlags;
	using FlagMember = bool Flags::*;

	struct PanelDesc
	{
		const char* id;					// 窗口 ID（稳定，标题改了也不影响 imgui.ini 里记住的位置）
		const char* title;				// 菜单项 / 窗口标题
		int group;						// 菜单分组（组之间加分隔线）
		const char* flagName;			// 关联的 PostProcessFlags 字段名，nullptr = 没有功能开关
		FlagMember flag;
		const char* note;				// 面板顶部的说明，可为 nullptr
		bool (*draw)(Scene3DRenderOption&);
		float defaultHeight;
		Scene3DRenderOption::DenoiseDebugTarget denoiseTarget = Scene3DRenderOption::DenoiseDebugTarget::None;	// 有降噪调试的面板
	};

	// 参数绘制沿用原来 Options 里的 PropertiesHelper::DrawData（通用反射绘制 / DDGI 的手写特化）
	constexpr size_t kPanelCount = 11;
	const std::array<PanelDesc, kPanelCount> kPanels = { {
		{ "Transparent", "半透明", 0, "drawTransparent", &Flags::drawTransparent, nullptr,
			[](Scene3DRenderOption& o) { return PropertiesHelper::DrawData(o.transparentParams); }, 160.0f },
		{ "SSAO", "SSAO", 0, nullptr, nullptr, "SSAO 没有单独的开关，始终开启",
			[](Scene3DRenderOption& o) { return PropertiesHelper::DrawData(o.ssaoParams); }, 180.0f },

		{ "Atmosphere", "大气", 1, "atmosphereOn", &Flags::atmosphereOn, nullptr,
			[](Scene3DRenderOption& o) { return PropertiesHelper::DrawData(o.atmosphereParams); }, 380.0f },
		{ "ExponentialHeightFog", "指数高度雾", 1, "exponentialHeightFogOn", &Flags::exponentialHeightFogOn, nullptr,
			[](Scene3DRenderOption& o) { return PropertiesHelper::DrawData(o.exponentialHeightFogParams); }, 330.0f },
		{ "VolumetricCloud", "体积云", 1, "volumetricCloudOn", &Flags::volumetricCloudOn, nullptr,
			[](Scene3DRenderOption& o) { return PropertiesHelper::DrawData(o.volumetricCloudParams); }, 560.0f },

		{ "SSR", "屏幕空间反射（SSR）", 2, "ssrOn", &Flags::ssrOn, nullptr,
			[](Scene3DRenderOption& o) { return PropertiesHelper::DrawData(o.ssrTraceParams); }, 520.0f, Scene3DRenderOption::DenoiseDebugTarget::SSR },
		{ "SSGI", "屏幕空间 GI（SSGI）", 2, "ssgiOn", &Flags::ssgiOn, nullptr,
			[](Scene3DRenderOption& o) { return PropertiesHelper::DrawData(o.ssgiTraceParams); }, 560.0f, Scene3DRenderOption::DenoiseDebugTarget::SSGI },

		{ "RayTraceGeneral", "光追通用", 3, nullptr, nullptr, "光追反射 / 光追 GI / DDGI 共用：进入加速结构的物体离相机的最大距离",
			[](Scene3DRenderOption& o) { return PropertiesHelper::DrawData(o.rayTraceGeneralParams); }, 180.0f },
		{ "RayTraceReflect", "光追反射（RTR）", 3, "rayTraceReflectOn", &Flags::rayTraceReflectOn, nullptr,
			[](Scene3DRenderOption& o) { return PropertiesHelper::DrawData(o.rayTraceReflectParams); }, 500.0f, Scene3DRenderOption::DenoiseDebugTarget::RayTraceReflect },
		{ "RayTraceGI", "光追 GI（RTGI）", 3, "rayTraceGIOn", &Flags::rayTraceGIOn, nullptr,
			[](Scene3DRenderOption& o) { return PropertiesHelper::DrawData(o.rayTraceGIParams); }, 520.0f, Scene3DRenderOption::DenoiseDebugTarget::RayTraceGI },

		{ "DDGI", "DDGI", 4, "ddgiOn", &Flags::ddgiOn, nullptr,
			[](Scene3DRenderOption& o) { return PropertiesHelper::DrawData(o.ddgiParams); }, 560.0f },
	} };

	// PostProcessFlags 里没有参数面板、但有别的窗口可以打开的开关
	constexpr std::string_view kProbeDrawFlag = "ddgiProbeDrawOn";		// → DDGI 调试

	struct PanelState
	{
		bool visible = false;
		bool focus = false;		// 下一帧置前（从菜单 / 入口按钮打开时）
		bool wasVisible = false;	// 上一帧是否打开，用来检测「刚被关掉」
	};

	std::array<PanelState, kPanelCount>& States()
	{
		static std::array<PanelState, kPanelCount> states;
		return states;
	}

	void Open(size_t index)
	{
		auto& s = States()[index];
		s.visible = true;
		s.focus = true;
	}

	int FindPanelByFlag(std::string_view flagName)
	{
		for (size_t i = 0; i < kPanels.size(); ++i)
		{
			if (kPanels[i].flagName && flagName == kPanels[i].flagName)
				return int(i);
		}
		return -1;
	}

	// 定义在下方（DrawPanel 之后），这里先声明
	bool DrawDenoiseDebug(Scene3DRenderOption& option, const PanelDesc& desc);

	void DrawPanel(EditorApp* app, size_t index)
	{
		const PanelDesc& desc = kPanels[index];
		auto& s = States()[index];

		const std::string windowName = std::string(desc.title) + " 参数###RenderParam_" + desc.id;
		ImGui::SetNextWindowSize(ImVec2(420.0f, desc.defaultHeight), ImGuiCond_FirstUseEver);
		if (s.focus)
		{
			ImGui::SetNextWindowFocus();
			s.focus = false;
		}

		// 独立浮动窗口，不参与停靠
		if (!ImGui::Begin(windowName.c_str(), &s.visible, ImGuiWindowFlags_NoDocking))
		{
			ImGui::End();
			return;
		}

		if (!app)
		{
			ImGui::TextDisabled("渲染器未就绪");
			ImGui::End();
			return;
		}

		auto option = app->GetOption();
		bool changed = false;

		ImGui::PushID(desc.id);

		if (desc.flag)
		{
			// 与 PostProcessFlags 里的是同一个字段
			const std::string label = std::string("启用（") + desc.flagName + "）";
			changed |= ImGui::Checkbox(label.c_str(), &(option.flags.*desc.flag));
			if (!(option.flags.*desc.flag))
			{
				ImGui::SameLine();
				ImGui::TextDisabled("未开启，参数开启后生效");
			}
		}

		if (desc.flag == &Flags::ddgiOn)
		{
			changed |= ImGui::Checkbox("探针可视化（ddgiProbeDrawOn）", &option.flags.ddgiProbeDrawOn);
			ImGui::SameLine();
			if (ImGui::SmallButton("DDGI 调试"))
				DDGIEditor::DebugWindowVisible() = true;
		}

		if (desc.note)
			ImGui::TextDisabled("%s", desc.note);

		ImGui::Separator();

		changed |= desc.draw(option);

		if (desc.denoiseTarget != Scene3DRenderOption::DenoiseDebugTarget::None)
			changed |= DrawDenoiseDebug(option, desc);

		ImGui::PopID();

		if (changed)
			app->SetOption(option);

		ImGui::End();
	}

	using DenoiseTarget = Scene3DRenderOption::DenoiseDebugTarget;
	using DenoiseMode = Scene3DRenderOption::DenoiseDebugMode;

	const char* DenoiseTargetName(DenoiseTarget target)
	{
		switch (target)
		{
		case DenoiseTarget::RayTraceReflect: return "光追反射（RTR）";
		case DenoiseTarget::RayTraceGI: return "光追 GI（RTGI）";
		case DenoiseTarget::SSR: return "屏幕空间反射（SSR）";
		case DenoiseTarget::SSGI: return "屏幕空间 GI（SSGI）";
		default: return "无";
		}
	}

	// 被调试 Pass 的功能开关（与 Scene3DRenderOption::IsDenoiseDebugTargetOn 对应）
	FlagMember DenoiseTargetFlag(DenoiseTarget target)
	{
		switch (target)
		{
		case DenoiseTarget::RayTraceReflect: return &Flags::rayTraceReflectOn;
		case DenoiseTarget::RayTraceGI: return &Flags::rayTraceGIOn;
		case DenoiseTarget::SSR: return &Flags::ssrOn;
		case DenoiseTarget::SSGI: return &Flags::ssgiOn;
		default: return nullptr;
		}
	}

	// 同分支的另一个 Pass（RTR 与 SSR、RTGI 与 SSGI）：开着时它的结果会叠加在调试画面上
	DenoiseTarget DenoisePartnerTarget(DenoiseTarget target)
	{
		switch (target)
		{
		case DenoiseTarget::RayTraceReflect: return DenoiseTarget::SSR;
		case DenoiseTarget::SSR: return DenoiseTarget::RayTraceReflect;
		case DenoiseTarget::RayTraceGI: return DenoiseTarget::SSGI;
		case DenoiseTarget::SSGI: return DenoiseTarget::RayTraceGI;
		default: return DenoiseTarget::None;
		}
	}

	// 时域累积相关参数（四个 Pass 的参数结构里字段同名），用来在说明里显示当前值
	struct TemporalParamsInfo
	{
		float depthThreshold = 0.0f;
		float positiondistThreshold = 0.0f;
		float normalThreshold = 0.0f;
		float motionWeightScale = 0.0f;
		float varianceClipGamma = 0.0f;
		uint32_t maxAccumulateCount = 0;
	};

	template<typename ParamsT>
	TemporalParamsInfo MakeTemporalParamsInfo(const ParamsT& p)
	{
		TemporalParamsInfo info;
		info.depthThreshold = p.depthThreshold;
		info.positiondistThreshold = p.positiondistThreshold;
		info.normalThreshold = p.normalThreshold;
		info.motionWeightScale = p.motionWeightScale;
		info.varianceClipGamma = p.varianceClipGamma;
		info.maxAccumulateCount = p.maxAccumulateCount;
		return info;
	}

	TemporalParamsInfo GetTemporalParamsInfo(const Scene3DRenderOption& option, DenoiseTarget target)
	{
		switch (target)
		{
		case DenoiseTarget::RayTraceReflect: return MakeTemporalParamsInfo(option.rayTraceReflectParams);
		case DenoiseTarget::RayTraceGI: return MakeTemporalParamsInfo(option.rayTraceGIParams);
		case DenoiseTarget::SSR: return MakeTemporalParamsInfo(option.ssrTraceParams);
		case DenoiseTarget::SSGI: return MakeTemporalParamsInfo(option.ssgiTraceParams);
		default: return {};
		}
	}

	struct DenoiseModeInfo
	{
		const char* name;
		const char* summary;	// 下拉框里的悬停简介
	};
	const DenoiseModeInfo kDenoiseModes[] = {
		{ "关闭", "正常显示场景" },
		{ "历史长度", "每个像素已经连续累积了多少帧（越长越干净，但响应越慢）" },
		{ "拒绝原因", "历史为什么没用上：重投影后哪一项几何检测没通过" },
		{ "裁剪量", "方差裁剪把历史颜色往当前帧拉回了多少（抑制拖影）" },
		{ "历史通过权重", "这一帧能用上的历史比例（2×2 重投影里通过检测的权重）" },
	};
	constexpr int kDenoiseModeCount = int(sizeof(kDenoiseModes) / sizeof(kDenoiseModes[0]));

	const ImVec4 kDenoiseWarnColor(0.95f, 0.65f, 0.25f, 1.0f);
	const ImVec4 kDenoiseErrorColor(1.0f, 0.4f, 0.4f, 1.0f);
	const ImVec4 kDenoiseOkColor(0.45f, 0.85f, 0.45f, 1.0f);

	template<typename... Args>
	std::string Format(const char* fmt, Args... args)
	{
		char buf[512];
		std::snprintf(buf, sizeof(buf), fmt, args...);
		return buf;
	}

	void TextWrappedColored(const ImVec4& color, const std::string& text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::TextWrapped("%s", text.c_str());
		ImGui::PopStyleColor();
	}

	void BulletWrapped(const std::string& text)
	{
		ImGui::Bullet();
		ImGui::TextWrapped("%s", text.c_str());
	}

	// 与 Temporal/TemporalAccumulate.comp 的 DebugHeat 一致：0 → 黑；(0, 1] → 蓝 → 青 → 绿 → 黄 → 红
	ImVec4 DebugHeat(float t)
	{
		if (t <= 0.0f)
			return ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
		t = std::clamp(t, 0.0f, 1.0f);
		auto channel = [t](float c) { return std::clamp(1.5f - std::abs(4.0f * t - c), 0.0f, 1.0f); };
		return ImVec4(channel(3.0f), channel(2.0f), channel(1.0f), 1.0f);
	}

	// 图例：色块 + 说明
	void LegendSwatch(const ImVec4& color, const std::string& text)
	{
		const float size = ImGui::GetTextLineHeight();
		ImGui::PushID(text.c_str());
		ImGui::ColorButton("##swatch", color, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_NoAlpha, ImVec2(size, size));
		ImGui::PopID();
		ImGui::SameLine();
		ImGui::TextWrapped("%s", text.c_str());
	}

	// 图例：热力图色带，下面两端各标一段文字
	void LegendHeatBar(const std::string& left, const std::string& right)
	{
		const float width = std::max(ImGui::GetContentRegionAvail().x, 80.0f);
		const float height = ImGui::GetTextLineHeight();
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		constexpr int kSegments = 32;
		for (int i = 0; i < kSegments; ++i)
		{
			const float t0 = float(i) / kSegments;
			const float t1 = float(i + 1) / kSegments;
			const ImU32 c0 = ImGui::ColorConvertFloat4ToU32(DebugHeat(std::max(t0, 1e-3f)));
			const ImU32 c1 = ImGui::ColorConvertFloat4ToU32(DebugHeat(t1));
			drawList->AddRectFilledMultiColor(ImVec2(p0.x + width * t0, p0.y), ImVec2(p0.x + width * t1, p0.y + height), c0, c1, c1, c0);
		}
		ImGui::Dummy(ImVec2(width, height));

		ImGui::TextDisabled("%s", left.c_str());
		const float rightWidth = ImGui::CalcTextSize(right.c_str()).x;
		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - rightWidth));
		ImGui::TextDisabled("%s", right.c_str());
	}

	// 选中某个调试视图后的说明：看什么 / 颜色含义 / 怎么看
	void DrawDenoiseModeExplanation(DenoiseMode mode, const TemporalParamsInfo& p)
	{
		ImGui::SeparatorText("看什么");
		switch (mode)
		{
		case DenoiseMode::HistoryLength:
			ImGui::TextWrapped("每个像素已经连续累积了多少帧。时域降噪靠把多帧结果平均来压噪点：历史越长画面越干净，但对光照、物体变化的响应也越慢（拖影）。");
			break;
		case DenoiseMode::RejectReason:
			ImGui::TextWrapped("当前像素按运动矢量找到上一帧的位置后，取周围 2×2 个历史采样点逐一做几何检测，判断它们是不是同一个表面。每个采样点按第一个没通过的检测上色，再按双线性权重混合。用来查「这里为什么没有历史」：闪烁、噪点一直不收敛、边缘一直糊。");
			break;
		case DenoiseMode::ClipAmount:
			ImGui::TextWrapped("方差裁剪：用当前帧 3×3 邻域颜色的均值 ± γ·σ 构成一个颜色盒子，历史颜色落在盒子外就被拉回到盒子边上。它能抑制拖影和鬼影，但裁得太多，累积的历史就白费了，噪点会变多。");
			break;
		case DenoiseMode::HistoryWeight:
			ImGui::TextWrapped("2×2 重投影里通过检测的采样点的双线性权重之和，也就是这一帧真正能用上的历史比例。和「拒绝原因」是同一件事的两种看法：这里看「用上了多少」，拒绝原因看「为什么没用上」。");
			break;
		default:
			break;
		}

		ImGui::SeparatorText("颜色含义");
		switch (mode)
		{
		case DenoiseMode::HistoryLength:
			LegendSwatch(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), "黑：没有历史（这一帧刚被重置）");
			LegendHeatBar("2 帧", Format("%u 帧（maxAccumulateCount）", p.maxAccumulateCount));
			break;
		case DenoiseMode::RejectReason:
			LegendSwatch(ImVec4(0.12f, 0.12f, 0.12f, 1.0f), "灰：通过，历史可用");
			LegendSwatch(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), Format("红：法线差太大（normalThreshold = %.3f，越接近 1 越严格）", p.normalThreshold));
			LegendSwatch(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), Format("绿：平面距离超出，换了表面 / 前后遮挡（depthThreshold = %.3f，按视深比例）", p.depthThreshold));
			LegendSwatch(ImVec4(0.0f, 0.0f, 1.0f, 1.0f), Format("蓝：世界距离超出，多半是运动矢量误差（positiondistThreshold = %.3f）", p.positiondistThreshold));
			LegendSwatch(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), "品红：历史位置上一帧是天空");
			LegendSwatch(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "白：重投影到了屏幕外（镜头转动时画面边缘）");
			break;
		case DenoiseMode::ClipAmount:
			LegendSwatch(ImVec4(0.06f, 0.06f, 0.06f, 1.0f), "暗灰：历史在盒子里，没被裁剪");
			LegendSwatch(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), "黑：没有历史，谈不上裁剪");
			LegendHeatBar("超出盒子 1 倍半径", "超出 4 倍及以上");
			break;
		case DenoiseMode::HistoryWeight:
			LegendSwatch(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), "黑：0，没有可用历史（低于 0.01 也按没有历史处理）");
			LegendHeatBar("接近 0", "1（4 个采样点全部通过）");
			break;
		default:
			break;
		}
		ImGui::TextDisabled("天空像素不做时域累积，在所有视图里都是黑的。");

		ImGui::SeparatorText("怎么看");
		switch (mode)
		{
		case DenoiseMode::HistoryLength:
			BulletWrapped("画面静止几秒后应该整片变红；一直停在蓝、绿，说明历史总被重置，切到「拒绝原因」看是哪项检测没通过。");
			BulletWrapped("物体边缘、刚露出来的区域（去遮挡）短一圈是正常的。");
			if (p.motionWeightScale > 0.0f)
				BulletWrapped(Format("一移动镜头就整片变短：motionWeightScale（当前 %.3f）会按运动的像素数衰减历史，调小或设为 0 可以减轻。", p.motionWeightScale));
			else
				BulletWrapped("motionWeightScale 当前为 0，镜头运动本身不会缩短历史；移动时变短是重投影检测没通过导致的。");
			break;
		case DenoiseMode::RejectReason:
			BulletWrapped("颜色是 4 个采样点的混合：暗红、暗绿这种说明只有一部分采样点没通过，问题通常不大。");
			BulletWrapped("物体轮廓上出现一圈彩色是正常的（采样点落到了别的表面上）。");
			BulletWrapped("平面中间大片发红或发绿，一般是阈值太严，或者 GBuffer 法线 / 位置精度不够。");
			BulletWrapped("镜头移动时大片发蓝，先检查运动矢量对不对，再考虑放宽 positiondistThreshold。");
			break;
		case DenoiseMode::ClipAmount:
			BulletWrapped("运动物体的拖尾、光照突变的地方发红是正常的，说明裁剪在起作用。");
			BulletWrapped(Format("画面静止时也大片发红：varianceClipGamma（当前 %.2f）偏小，裁剪太狠，可以调大；拖影明显就调小。", p.varianceClipGamma));
			if (p.varianceClipGamma <= 0.0f)
				TextWrappedColored(kDenoiseWarnColor, "varianceClipGamma ≤ 0，方差裁剪已关闭，这个视图会是整片暗灰。");
			break;
		case DenoiseMode::HistoryWeight:
			BulletWrapped("物体轮廓上一圈低值是正常的。");
			BulletWrapped("平面中间也偏低，说明检测阈值太严，切到「拒绝原因」看具体是哪一项。");
			break;
		default:
			break;
		}
	}

	// 降噪调试：选一个模式后，这个 Pass 时域累积的内部状态替换场景颜色显示（同一时刻只调试一个 Pass）
	bool DrawDenoiseDebug(Scene3DRenderOption& option, const PanelDesc& desc)
	{
		auto& debug = option.denoiseDebug;
		bool changed = false;

		ImGui::Separator();
		if (!ImGui::CollapsingHeader("降噪调试"))
			return false;

		const int currentMode = (debug.target == desc.denoiseTarget && debug.Active()) ? int(debug.mode) : 0;
		PropertiesHelper::SetNextPropertyFieldWidth(200.0f, 0.55f);
		if (ImGui::BeginCombo("显示", kDenoiseModes[currentMode].name))
		{
			for (int i = 0; i < kDenoiseModeCount; ++i)
			{
				const bool selected = i == currentMode;
				if (ImGui::Selectable(kDenoiseModes[i].name, selected) && !selected)
				{
					if (i == 0)
					{
						if (debug.target == desc.denoiseTarget)
							debug = {};
					}
					else
					{
						debug.target = desc.denoiseTarget;
						debug.mode = DenoiseMode(i);
					}
					changed = true;
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", kDenoiseModes[i].summary);
			}
			ImGui::EndCombo();
		}

		if (debug.Active() && debug.target != desc.denoiseTarget)
			TextWrappedColored(kDenoiseWarnColor, Format("正在调试：%s（在这里选一个模式会切换过来）", DenoiseTargetName(debug.target)));

		const bool active = debug.target == desc.denoiseTarget && debug.Active();
		if (!active)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("选一个模式，把这个 Pass 时域降噪（多帧累积）的内部状态画成热力图替换场景颜色，用来排查拖影、闪烁、噪点不收敛。鼠标停在选项上可以看简介。");
			ImGui::PopStyleColor();
			return changed;
		}

		// 被调试的 Pass 没开：渲染器不会替换场景颜色（见 Scene3DRenderOption::DenoiseDebugEffective），这里明确提示
		const FlagMember flag = DenoiseTargetFlag(desc.denoiseTarget);
		if (!option.IsDenoiseDebugTargetOn(desc.denoiseTarget))
		{
			TextWrappedColored(kDenoiseErrorColor, Format("「%s」没有开启：这个 Pass 不执行，没有调试数据可看，画面保持正常显示。开启后调试视图才会生效。", DenoiseTargetName(desc.denoiseTarget)));
			if (flag && ImGui::Button("开启这个 Pass"))
			{
				option.flags.*flag = true;
				changed = true;
			}
		}
		else
		{
			TextWrappedColored(kDenoiseOkColor, "调试视图已生效：画面显示的是热力图，不是真实光照。曝光 / 伽马仍会作用在上面。");

			const DenoiseTarget partner = DenoisePartnerTarget(desc.denoiseTarget);
			const FlagMember partnerFlag = DenoiseTargetFlag(partner);
			if (partnerFlag && option.IsDenoiseDebugTargetOn(partner))
			{
				TextWrappedColored(kDenoiseWarnColor, Format("「%s」也开着，它的结果会叠加在调试画面上，看不清时先关掉它。", DenoiseTargetName(partner)));
				const std::string label = Format("关闭%s", DenoiseTargetName(partner));
				if (ImGui::SmallButton(label.c_str()))
				{
					option.flags.*partnerFlag = false;
					changed = true;
				}
			}
		}

		DrawDenoiseModeExplanation(debug.mode, GetTemporalParamsInfo(option, desc.denoiseTarget));

		return changed;
	}

	// 参数窗口被关掉（标题栏 ×、菜单取消勾选）时，退出它正在显示的降噪调试视图，
	// 免得窗口没了画面还停在热力图上、找不到地方关
	void ExitDenoiseDebugFor(EditorApp* app, const PanelDesc& desc)
	{
		if (!app || desc.denoiseTarget == DenoiseTarget::None)
			return;
		auto option = app->GetOption();
		if (option.denoiseDebug.target != desc.denoiseTarget)
			return;
		option.denoiseDebug = {};
		app->SetOption(option);
	}

	// 入口按钮：flag 有关联面板（或 DDGI 调试）时画在同一行
	void DrawFlagEntry(std::string_view flagName, float column)
	{
		const int panel = FindPanelByFlag(flagName);
		const bool probeDraw = flagName == kProbeDrawFlag;
		if (panel < 0 && !probeDraw)
			return;

		ImGui::SameLine(column);
		const std::string label = std::string(probeDraw ? "调试" : "参数") + "##entry_" + std::string(flagName);
		if (ImGui::SmallButton(label.c_str()))
		{
			if (probeDraw)
				DDGIEditor::DebugWindowVisible() = true;
			else
				Open(size_t(panel));
		}
		if (ImGui::IsItemHovered())
		{
			if (probeDraw)
				ImGui::SetTooltip("打开 DDGI 调试窗口");
			else
				ImGui::SetTooltip("打开「%s」参数面板", kPanels[size_t(panel)].title);
		}
	}

	// 写成模板（同 PropertiesHelper::DrawComponent）：rfl 的字段推导在非模板函数里会被立即求值，
	// IntelliSense 推不出 rfl::fields 的类型（报「表达式必须含有常量值」），放进模板里就按实例化时再求值
	template<typename FlagsT>
	bool DrawFlagsImpl(FlagsT& flags)
	{
		bool changed = false;

		const auto fields = rfl::fields<FlagsT>();
		auto view = rfl::to_view(flags);
		constexpr std::size_t kFieldCount = std::tuple_size_v<std::remove_cvref_t<decltype(fields)>>;

		// 入口按钮对齐到同一列：勾选框 + 最长的字段名之后
		float maxLabel = 0.0f;
		for (std::size_t i = 0; i < kFieldCount; ++i)
			maxLabel = std::max(maxLabel, ImGui::CalcTextSize(fields[i].name().c_str()).x);
		const ImGuiStyle& style = ImGui::GetStyle();
		const float column = ImGui::GetCursorPosX() + ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + maxLabel + style.ItemSpacing.x * 2.0f;

		[&] <std::size_t... I>(std::index_sequence<I...>) {
			((changed |= [&]() -> bool {
				const std::string& name = fields[I].name();
				auto* ptr = view.template get<I>();
				bool c = false;
				if constexpr (std::is_same_v<std::remove_cvref_t<std::remove_pointer_t<decltype(ptr)>>, bool>)
				{
					c = ImGui::Checkbox(name.c_str(), ptr);
					DrawFlagEntry(name, column);
				}
				return c;
				}()), ...);
		}(std::make_index_sequence<kFieldCount>{});

		return changed;
	}
}

namespace RenderOptionPanels
{
	void DrawMenu()
	{
		if (!ImGui::BeginMenu("渲染参数"))
			return;

		int group = kPanels.empty() ? 0 : kPanels[0].group;
		for (size_t i = 0; i < kPanels.size(); ++i)
		{
			if (kPanels[i].group != group)
			{
				ImGui::Separator();
				group = kPanels[i].group;
			}

			auto& s = States()[i];
			const bool wasVisible = s.visible;
			ImGui::MenuItem(kPanels[i].title, nullptr, &s.visible);
			if (s.visible && !wasVisible)
				s.focus = true;
		}

		ImGui::EndMenu();
	}

	void Draw(EditorApp* app)
	{
		for (size_t i = 0; i < kPanels.size(); ++i)
		{
			auto& s = States()[i];
			if (s.visible)
				DrawPanel(app, i);

			// 不管是点 × 还是在菜单里取消勾选，都在「打开 → 关闭」这一刻退出对应的调试视图
			if (s.wasVisible && !s.visible)
				ExitDenoiseDebugFor(app, kPanels[i]);
			s.wasVisible = s.visible;
		}
	}

	bool OpenForFlag(std::string_view flagName)
	{
		const int panel = FindPanelByFlag(flagName);
		if (panel < 0)
			return false;
		Open(size_t(panel));
		return true;
	}

	bool DrawFlags(Scene3DRenderOption& option)
	{
		return DrawFlagsImpl(option.flags);
	}
}

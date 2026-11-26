#include "MaterialPreview.h"

#include "Sprite2DEditor.h"
#include "VulkanRenderEngine/RenderPass/MaterialPreviewPass.h"
#include "VulkanRenderEngine/Base/Material.h"
#include "VulkanRenderEngine/Base/Texture2D.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace
{
	// 输出贴图轮流用的张数和复用间隔：一张贴图最后一次显示之后，至少隔这么多个编辑器帧才往上重画，
	// 保证 GPU 已经画完用到它的那几帧（编辑器交换链帧并行 + ImGuiTextureCache 推迟释放 3 帧）
	constexpr size_t kRingSize = 4;
	constexpr int kReuseAfterFrames = 4;

	// 输出尺寸按 32 取整，拖窗口大小时不会每帧都重建贴图
	constexpr uint32_t kMinSize = 128;
	constexpr uint32_t kMaxSize = 512;
	constexpr uint32_t kSizeStep = 32;

	constexpr float kMinDistance = 2.2f;
	constexpr float kMaxDistance = 10.0f;

	// 每种网格自己的默认视角：
	//   球体：侧上方一点，看得到高光和明暗过渡；
	//   立方体：对着一条棱、从上往下，三个面都看得到；
	//   平面：正对正面（平面正面朝 +Z，yaw = 0 / pitch = 0 就是正视），看贴图本身不变形
	void ApplyDefaultCamera(MaterialPreviewView& view)
	{
		const MaterialPreviewView defaults;
		view.distance = defaults.distance;
		switch (view.shape)
		{
		case MaterialPreviewShape::Plane:
			view.yaw = 0.0f;
			view.pitch = 0.0f;
			break;
		case MaterialPreviewShape::Cube:
			view.yaw = -35.0f;
			view.pitch = 25.0f;
			break;
		default:
			view.yaw = defaults.yaw;
			view.pitch = defaults.pitch;
			break;
		}
	}

	MaterialPreviewView DefaultView(MaterialPreviewShape shape = MaterialPreviewShape::Sphere)
	{
		MaterialPreviewView view;
		view.shape = shape;
		ApplyDefaultCamera(view);
		view.lightFollowCamera = true;	// 编辑器里默认灯跟着视角转：转到哪边看都是正面打光
		view.lightYaw = -15.0f;
		view.lightPitch = 41.0f;
		return view;
	}

	float WrapDegrees(float degrees)
	{
		degrees = std::fmod(degrees + 180.0f, 360.0f);
		if (degrees < 0.0f)
			degrees += 360.0f;
		return degrees - 180.0f;
	}

	struct TextureKey
	{
		TextureType type = TextureType::Albedo;
		const Texture2D* texture = nullptr;
		uint32_t version = 0;
		bool operator==(const TextureKey&) const = default;
	};

	// 决定要不要重画的全部输入
	struct Snapshot
	{
		AssetGUID guid;
		const Material* material = nullptr;
		uint32_t materialVersion = 0;
		std::vector<TextureKey> textures;
		MaterialPreviewView view;
		uint32_t size = 0;
	};

	bool SameView(const MaterialPreviewView& a, const MaterialPreviewView& b)
	{
		return a.shape == b.shape
			&& a.yaw == b.yaw && a.pitch == b.pitch && a.distance == b.distance && a.fovY == b.fovY
			&& a.lightYaw == b.lightYaw && a.lightPitch == b.lightPitch && a.lightFollowCamera == b.lightFollowCamera
			&& a.exposure == b.exposure && a.supersample == b.supersample;
	}

	bool Same(const Snapshot& a, const Snapshot& b)
	{
		return a.guid == b.guid && a.material == b.material && a.materialVersion == b.materialVersion
			&& a.textures == b.textures && a.size == b.size && SameView(a.view, b.view);
	}

	struct State
	{
		MaterialPreviewView view = DefaultView();
		bool checker = true;

		std::array<std::shared_ptr<Texture2D>, kRingSize> ring;
		std::array<int, kRingSize> lastShownFrame{};		// 每张最后一次交给 ImGui 显示的帧号
		int current = -1;									// 正在显示的那张

		Snapshot last;
		bool hasLast = false;
		bool failed = false;
	};

	State& GetState()
	{
		static State state;
		return state;
	}

	void ReleaseImages(State& state)
	{
		for (auto& texture : state.ring)
		{
			if (texture)
				Sprite2DEditor::TextureCache().Remove(texture);	// 描述符集照样推迟释放，期间继续持有贴图
			texture.reset();
		}
		state.lastShownFrame.fill(0);
		state.current = -1;
		state.hasLast = false;
		state.failed = false;
	}

	uint32_t QuantizeSize(float side)
	{
		uint32_t pixels = static_cast<uint32_t>(std::ceil(std::max(side, 1.0f)));
		pixels = (pixels + kSizeStep - 1) / kSizeStep * kSizeStep;
		return std::clamp(pixels, kMinSize, kMaxSize);
	}

	// 找一张可以写的：不是正在显示的、并且最后一次显示已经过了复用间隔。都不行就这一帧先不画
	int PickWritableSlot(const State& state)
	{
		const int frame = ImGui::GetFrameCount();
		constexpr int ringSize = static_cast<int>(kRingSize);
		for (int step = 1; step <= ringSize; ++step)
		{
			const int slot = (state.current + step + ringSize) % ringSize;
			if (slot == state.current)
				continue;
			if (!state.ring[slot] || frame - state.lastShownFrame[slot] >= kReuseAfterFrames)
				return slot;
		}
		return -1;
	}

	bool RenderPreview(State& state, const Material& material, const std::map<TextureType, std::shared_ptr<Texture2D>>& textures, const Snapshot& snapshot, bool& deferred)
	{
		deferred = false;
		const int slot = PickWritableSlot(state);
		if (slot < 0)
		{
			deferred = true;
			return false;
		}

		auto& target = state.ring[slot];
		if (!target || target->GetWidth() != snapshot.size || target->GetHeight() != snapshot.size)
		{
			if (target)
				Sprite2DEditor::TextureCache().Remove(target);
			// 只给 ImGui 画：不建采样器
			TextureConfig config;
			config.minFilter = vk::Filter::eLinear;
			config.magFilter = vk::Filter::eLinear;
			target = std::make_shared<Texture2D>(snapshot.size, snapshot.size, vk::Format::eR8G8B8A8Unorm, config, 1, false);
			if (target->IsEmpty())
			{
				target.reset();
				return false;
			}
		}

		MaterialPreviewInput input;
		input.properties = material.GetProperties();
		input.textures = textures;
		if (!MaterialPreviewPass::Instance().Render(input, snapshot.view, target))
			return false;

		state.current = slot;
		return true;
	}

	void DrawBackground(ImDrawList* drawList, const ImVec2& p0, const ImVec2& p1, bool checker)
	{
		drawList->AddRectFilled(p0, p1, IM_COL32(40, 40, 40, 255), 3.0f);
		if (!checker)
			return;
		// 半透明材质看得出透明度
		constexpr float cell = 12.0f;
		drawList->PushClipRect(p0, p1, true);
		for (float y = p0.y; y < p1.y; y += cell)
		{
			for (float x = p0.x; x < p1.x; x += cell)
			{
				const int ix = static_cast<int>((x - p0.x) / cell);
				const int iy = static_cast<int>((y - p0.y) / cell);
				if (((ix + iy) & 1) == 0)
					drawList->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + cell, p1.x), std::min(y + cell, p1.y)), IM_COL32(58, 58, 58, 255));
			}
		}
		drawList->PopClipRect();
	}

	void HandleCanvasInput(State& state, bool hovered, bool active)
	{
		MaterialPreviewView& view = state.view;
		const ImGuiIO& io = ImGui::GetIO();

		if (active && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
		{
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				view.yaw = WrapDegrees(view.yaw - io.MouseDelta.x * 0.4f);
				view.pitch = std::clamp(view.pitch + io.MouseDelta.y * 0.4f, -85.0f, 85.0f);
			}
			else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
			{
				view.lightYaw = WrapDegrees(view.lightYaw - io.MouseDelta.x * 0.5f);
				view.lightPitch = std::clamp(view.lightPitch - io.MouseDelta.y * 0.5f, -10.0f, 89.0f);
			}
		}

		if (hovered && io.MouseWheel != 0.0f)
			view.distance = std::clamp(view.distance * std::pow(0.9f, io.MouseWheel), kMinDistance, kMaxDistance);

		if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			ApplyDefaultCamera(view);		// 只复位视角，按当前网格
	}

	void DrawToolbar(State& state, float width)
	{
		MaterialPreviewView& view = state.view;
		ImGui::PushID("MaterialPreviewToolbar");

		static const char* kShapeNames[] = { "球体", "立方体", "平面" };
		static_assert(std::size(kShapeNames) == static_cast<size_t>(MaterialPreviewShape::Count));
		for (size_t i = 0; i < std::size(kShapeNames); ++i)
		{
			if (i > 0)
				ImGui::SameLine();
			const auto shape = static_cast<MaterialPreviewShape>(i);
			if (ImGui::RadioButton(kShapeNames[i], view.shape == shape) && view.shape != shape)
			{
				view.shape = shape;
				ApplyDefaultCamera(view);	// 换网格时换到它的默认视角（平面正视），灯光不动
			}
		}
		ImGui::SameLine();
		ImGui::Checkbox("棋盘格", &state.checker);

		// 切换"跟随视角"时换算一下，灯光的实际方向保持不动
		bool follow = view.lightFollowCamera;
		if (ImGui::Checkbox("灯光跟随视角", &follow))
		{
			view.lightYaw = WrapDegrees(follow ? view.lightYaw - view.yaw : view.lightYaw + view.yaw);
			view.lightFollowCamera = follow;
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("勾上时灯光水平角是相对视角的偏移：转动视角时灯光跟着转");

		const float itemWidth = std::max(80.0f, width * 0.55f);
		ImGui::SetNextItemWidth(itemWidth);
		ImGui::SliderFloat("灯光水平", &view.lightYaw, -180.0f, 180.0f, "%.0f°");
		ImGui::SetNextItemWidth(itemWidth);
		ImGui::SliderFloat("灯光高度", &view.lightPitch, -10.0f, 89.0f, "%.0f°");
		ImGui::SetNextItemWidth(itemWidth);
		ImGui::DragFloat("曝光", &view.exposure, 0.01f, 0.1f, 8.0f, "%.2f");

		if (ImGui::Button("复位视角和灯光"))
			view = DefaultView(view.shape);
		ImGui::PopID();
	}
}

namespace MaterialPreview
{
	void Draw(const AssetGUID& guid, const std::shared_ptr<Material>& material, float side)
	{
		auto& state = GetState();
		side = std::max(side, 64.0f);

		ImGui::PushID("MaterialPreview");
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const ImVec2 p1(p0.x + side, p0.y + side);
		ImGui::InvisibleButton("##canvas", ImVec2(side, side), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();
		const bool visible = ImGui::IsItemVisible();
		ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);		// 悬停时滚轮给预览缩放，不滚动外面的面板
		HandleCanvasInput(state, hovered, active);
		if (hovered && !active)
			ImGui::SetItemTooltip("左键拖动：旋转视角\n右键拖动：旋转灯光\n滚轮：缩放\n双击：复位视角");

		// ---------------- 有变化且看得见才重画 ----------------
		if (material && visible)
		{
			Snapshot snapshot;
			snapshot.guid = guid;
			snapshot.material = material.get();
			snapshot.materialVersion = material->GetVersion();
			const auto textures = material->GetTextures();
			for (const auto& [type, texture] : textures)
			{
				if (texture)
					snapshot.textures.push_back({ type, texture.get(), texture->GetDescBindEntryVersion() });
			}
			snapshot.view = state.view;
			snapshot.size = QuantizeSize(side);

			if (!state.hasLast || !Same(snapshot, state.last))
			{
				bool deferred = false;
				const bool ok = RenderPreview(state, *material, textures, snapshot, deferred);
				if (!deferred)		// 没有可写的贴图时下一帧再试，其余情况（成功或失败）都记下，不重复尝试
				{
					state.last = std::move(snapshot);
					state.hasLast = true;
					state.failed = !ok;
				}
			}
		}

		// ---------------- 画 ----------------
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		DrawBackground(drawList, p0, p1, state.checker);
		if (material && state.current >= 0 && state.ring[state.current])
		{
			if (ImTextureID id = Sprite2DEditor::TextureCache().Get(state.ring[state.current]); id != 0)
			{
				drawList->AddImage(id, p0, p1);
				state.lastShownFrame[state.current] = ImGui::GetFrameCount();
			}
		}
		if (!material || state.failed)
		{
			const char* text = material ? "预览不可用（见控制台）" : "材质未加载";
			const ImVec2 size = ImGui::CalcTextSize(text);
			drawList->AddText(ImVec2(p0.x + (side - size.x) * 0.5f, p0.y + (side - size.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
		}
		drawList->AddRect(p0, p1, active ? IM_COL32(70, 130, 200, 255) : IM_COL32(95, 95, 95, 255), 3.0f);

		DrawToolbar(state, side);
		ImGui::PopID();
	}

	void Reset()
	{
		ReleaseImages(GetState());
	}

	void Shutdown()
	{
		ReleaseImages(GetState());
	}
}

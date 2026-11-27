#include "PropertiesHelper.h"
#include "ImguiLayout.h"
#include "SceneCodec/ComponentCodec.h"
#include "Render/Components/ParticleEmitter.h"
#include "PlatformRuntimeComponents.h"
#include "CommonComponent.h"
#include "Sprite2DEditor.h"
#include "AssetThumbnails.h"
#include "VulkanRenderEngine/GlobalConfig.h"
#include "PhysicsShapeFromModel.h"
#include "PhysicsDebugView.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <unordered_map>
#include "Helper/Tools.h"
#include "imgui_internal.h"	// BeginDragDropTargetCustom：选中槽的头部整块作为拖放目标

// ---------------- 旋转：编辑器里的欧拉角 ----------------
// 原来每帧都"四元数 → glm::eulerAngles → 拖动 → 四元数"：eulerAngles 的 y 用 asin 求，只在 [-90°, 90°]，
// y 一过 90° 就换成等价的 (x+180, 180-y, z+180) 写法，表现为 y 卡在 90、x / z 突然变成 ±180。
// 这里改成面板自己记住欧拉角：拖动、显示都用记住的值；只有四元数被别处改了（Gizmo、物理、脚本）才重新分解，
// 并且在所有等价写法里选和上次最接近的那个，数值连续。
namespace EditorRotation
{
	inline float NearestAngle(float value, float reference)
	{
		return value + 360.0f * std::round((reference - value) / 360.0f);
	}

	inline bool SameRotation(const glm::quat& a, const glm::quat& b)
	{
		return std::abs(glm::dot(a, b)) > 1.0f - 1.0e-6f;	// q 和 -q 是同一个朝向
	}

	// q 的欧拉角（度）。约定和 glm 一致：q == quat(radians(euler))，即 R = Rz · Ry · Rx。
	// 等价写法：(x, y, z) 和 (x+180, 180-y, z+180)，每个分量再加减 360 也一样；在里面选和 reference 最接近的。
	// y = ±90°（万向节锁）时只有 x - z（y = 90）或 x + z（y = -90）有意义：x 保持 reference.x，只解 z
	inline glm::vec3 NearestEuler(const glm::quat& q, const glm::vec3& reference)
	{
		const glm::dquat dq = glm::normalize(glm::dquat(q.w, q.x, q.y, q.z));
		const glm::dmat3 m = glm::mat3_cast(dq);		// m[列][行]
		const double sinY = std::clamp(-m[0][2], -1.0, 1.0);

		const auto fit = [&](double x, double y, double z)
			{
				return glm::vec3(NearestAngle(static_cast<float>(x), reference.x),
					NearestAngle(static_cast<float>(y), reference.y),
					NearestAngle(static_cast<float>(z), reference.z));
			};
		const auto distance = [&](const glm::vec3& e)
			{
				const glm::vec3 d = glm::abs(e - reference);
				return d.x + d.y + d.z;
			};

		if (std::abs(sinY) < 1.0 - 1.0e-7)
		{
			const double y = glm::degrees(std::asin(sinY));
			const double x = glm::degrees(std::atan2(m[1][2], m[2][2]));
			const double z = glm::degrees(std::atan2(m[0][1], m[0][0]));
			const glm::vec3 a = fit(x, y, z);
			const glm::vec3 b = fit(x + 180.0, 180.0 - y, z + 180.0);
			return distance(a) <= distance(b) ? a : b;
		}

		const double d = glm::degrees(std::atan2(m[1][0], m[1][1]));
		const double x = reference.x;
		return sinY > 0.0 ? fit(x, 90.0, x - d) : fit(x, -90.0, -d - x);
	}

	struct CacheEntry
	{
		glm::vec3 euler = glm::vec3(0.0f);
		glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		double lastEditTime = -1.0e9;
		int lastUsedFrame = 0;
	};

	// 按 ImGui ID（实体 + 组件 + 字段，调用方的 ID 栈决定）缓存
	inline std::unordered_map<ImGuiID, CacheEntry>& Cache()
	{
		static std::unordered_map<ImGuiID, CacheEntry> cache;
		return cache;
	}

	// 改完异步提交到场景线程，组件要过几帧才是新值：自己刚改过（含拖动中）这么久之内不按组件重新分解
	constexpr double kPendingSeconds = 0.5;

	// 拖欧拉角编辑一个四元数，返回是否改了（改了的话 rotation 已经写好）
	inline bool DragEuler(const char* label, glm::quat& rotation, float speed = 0.5f, const char* format = "%.1f°")
	{
		auto& cache = Cache();
		const ImGuiID id = ImGui::GetID(label);
		const int frame = ImGui::GetFrameCount();
		const double now = ImGui::GetTime();

		auto [it, inserted] = cache.try_emplace(id);
		CacheEntry& entry = it->second;
		if (inserted)
		{
			entry.euler = NearestEuler(rotation, glm::vec3(0.0f));		// 第一次：选最接近 0 的写法
			entry.rotation = rotation;
		}
		else if (!SameRotation(entry.rotation, rotation) && now - entry.lastEditTime > kPendingSeconds)
		{
			entry.euler = NearestEuler(rotation, entry.euler);			// 被别处改了：选和上次最接近的写法
			entry.rotation = rotation;
		}
		entry.lastUsedFrame = frame;

		glm::vec3 euler = entry.euler;
		const bool changed = ImGui::DragFloat3(label, glm::value_ptr(euler), speed, 0.0f, 0.0f, format);
		if (ImGui::IsItemActive())
			entry.lastEditTime = now;
		if (changed)
		{
			entry.euler = euler;
			entry.rotation = glm::normalize(glm::quat(glm::radians(euler)));
			entry.lastEditTime = now;
			rotation = entry.rotation;
		}

		// 很久没画的（换了选中、删了实体）清掉
		static int s_lastSweepFrame = 0;
		if (frame - s_lastSweepFrame > 600)
		{
			s_lastSweepFrame = frame;
			std::erase_if(cache, [frame](const auto& item) { return frame - item.second.lastUsedFrame > 600; });
		}
		return changed;
	}

	// 这一帧显示的欧拉角（在 DragEuler 之后、同一个 ID 栈里调用）
	inline glm::vec3 DisplayedEuler(const char* label)
	{
		auto& cache = Cache();
		auto it = cache.find(ImGui::GetID(label));
		return it != cache.end() ? it->second.euler : glm::vec3(0.0f);
	}

	// 直接设成某组欧拉角（复位按钮用）：缓存一起改，下一帧显示的就是这组值，不会被重新分解成别的写法。
	// 要在和 DragEuler 同一个 ID 栈里调用（同一个 label）
	inline void SetEuler(const char* label, glm::quat& rotation, const glm::vec3& euler)
	{
		CacheEntry& entry = Cache()[ImGui::GetID(label)];
		entry.euler = euler;
		entry.rotation = glm::normalize(glm::quat(glm::radians(euler)));
		entry.lastEditTime = ImGui::GetTime();
		entry.lastUsedFrame = ImGui::GetFrameCount();
		rotation = entry.rotation;
	}
}

template <typename T>
struct EditorSnapshot
{
	static void Make(const T& src, T& snapshot) { snapshot = src; }
	static void Apply(T&& snapshot, T& dst) { dst = std::move(snapshot); }
};

template <>
struct EditorSnapshot<UIView>
{
	static void Make(const UIView& src, UIView& snapshot)
	{
		snapshot.documentGuid = src.documentGuid;
		snapshot.viewName = src.viewName;
		snapshot.layer = src.layer;
		snapshot.capturesInput = src.capturesInput;
		snapshot.enabled = src.enabled;
		snapshot.needReload = src.needReload;
	}

	static void Apply(UIView&& snapshot, UIView& dst)
	{
		dst.documentGuid = snapshot.documentGuid;
		dst.viewName = snapshot.viewName;
		dst.layer = snapshot.layer;
		dst.capturesInput = snapshot.capturesInput;
		dst.enabled = snapshot.enabled;
		dst.needReload = snapshot.needReload;
	}
};

template <>
struct EditorSnapshot<Script>
{
	static void Make(const Script& src, Script& snapshot)
	{
		snapshot.data = src.data;
		snapshot.enabled = src.enabled;
		snapshot.needReload = src.needReload;
	}

	static void Apply(Script&& snapshot, Script& dst)
	{
		dst.data = snapshot.data;
		dst.enabled = snapshot.enabled;
		dst.needReload = snapshot.needReload;
	}
};

// 动图播放器：播放时间由系统每帧推进，快照写回时不能把它倒回去；其余运行时句柄也不覆盖
template <>
struct EditorSnapshot<AnimatedImagePlayer>
{
	static void Make(const AnimatedImagePlayer& src, AnimatedImagePlayer& snapshot) { snapshot = src; }

	static void Apply(AnimatedImagePlayer&& snapshot, AnimatedImagePlayer& dst)
	{
		const bool resumeFinished = !dst.playing && snapshot.playing && dst.finished;
		dst.visible = snapshot.visible;
		dst.layer = snapshot.layer;
		dst.internalZOrder = snapshot.internalZOrder;
		dst.offset = snapshot.offset;
		dst.imageGuid = snapshot.imageGuid;
		dst.playing = snapshot.playing;
		dst.loopCount = snapshot.loopCount;
		dst.speed = snapshot.speed;
		dst.size = snapshot.size;
		dst.pivot = snapshot.pivot;
		dst.color = snapshot.color;
		dst.flipX = snapshot.flipX;
		dst.flipY = snapshot.flipY;
		dst.blend = snapshot.blend;
		// 播完以后重新勾上"播放"等于从头播
		dst.restartRequested = dst.restartRequested || snapshot.restartRequested || resumeFinished;
	}
};

// 物理：碰撞形状是 shared_ptr，快照浅拷贝（每帧拷贝很便宜，三角网格的顶点也不复制）；
// 面板改某个形状时先复制一份再改（写时复制，见 DrawComponent<Physics>），不碰场景里正在用的那份。
// 写回时：forceSyncTransform / forceRecalculate 是场景线程上的标记，保留目标上的值，不让旧快照盖掉；
// 形状变了（任一子形状指针不同）就删掉 PhysicsRuntime——PhysicsSystem 的 updateRigidBody / updateCharacter
// 不会重建形状，删掉后下一次物理更新 checkNewEntity 按新形状重建刚体 / 角色（Bullet 对象在 OnRemove 里移除并释放）
template <>
struct EditorSnapshot<Physics>
{
	static void Make(const Physics& src, Physics& snapshot) { snapshot = src; }

	static void Apply(Physics&& snapshot, Physics& dst, const Entity& entity)
	{
		const bool shapesChanged = dst.collisionShape.children != snapshot.collisionShape.children;
		const bool syncTransform = dst.forceSyncTransform;
		const bool recalculate = dst.forceRecalculate;
		dst = std::move(snapshot);
		dst.forceSyncTransform = syncTransform;
		dst.forceRecalculate = recalculate;
		if (shapesChanged && entity.hasComponent<PhysicsRuntime>())
			entity.removeComponent<PhysicsRuntime>();
	}
};

// 移动：只写回能编辑的 canJump。移动方向 / 想跳是输入每帧写的运行时状态，不能让面板上一帧的旧值盖掉
template <>
struct EditorSnapshot<Movement>
{
	static void Make(const Movement& src, Movement& snapshot) { snapshot = src; }
	static void Apply(Movement&& snapshot, Movement& dst)
	{
		dst.canJump = snapshot.canJump;
		if (!dst.canJump)
			dst.currentWantJump = false;
	}
};

// 注册组件
template<typename T>
void PropertiesHelper::RegisterComponent()
{
	const ComponentTypeID typeID = ComponentType<T>::getId();
	auto& codecs = NComponentCodec::GetComponentCodecs();
	auto codec = std::find_if(codecs.begin(), codecs.end(), [typeID](const auto& item) {
		return item.componentType == typeID;
		});
	if (codec != codecs.end())
	{
		auto& registry = GetCustomDrawRegistry();
		registry[ComponentType<T>::getId()] = [](Entity entity, IComponent* comp, ProjectManager* assetManager) {
			if (!entity || !comp)
				return;

			if constexpr (sizeof(T) > sizeof(IComponent))
			{
				// 每帧一份快照：没改动时这一帧结束就释放；改了就移动进场景线程的命令
				// （SubmitCommand 收 std::move_only_function，可以捕获 unique_ptr），命令执行完释放
				auto temp = std::make_unique<T>();
				auto& component = *(static_cast<T*>(comp));
				EditorSnapshot<T>::Make(component, *temp);
				s_drawingEntity = entity;
				bool changed = DrawComponent(*temp, assetManager);
				s_drawingEntity = Entity();
				if (changed)
				{
					auto world = entity.getWorld();
					world->SubmitCommand([entity, temp = std::move(temp), &component] mutable ->void {
						// 需要实体的组件（例如 Physics 改了形状要重建运行时）提供三参数的 Apply
						if constexpr (requires { EditorSnapshot<T>::Apply(std::move(*temp), component, entity); })
							EditorSnapshot<T>::Apply(std::move(*temp), component, entity);
						else
							EditorSnapshot<T>::Apply(std::move(*temp), component);
						OnComponentChange(entity, component);
						});
				}
			}

			};
	}

}

template<typename Owner, typename FieldType>
bool PropertiesHelper::DrawPropertyField(const std::string& name, FieldType* value)
{
	const auto widget = PropertyUI<Owner>::GetWidget(name);
	return DrawField(name, value, widget);
}

template<typename FieldType>
bool PropertiesHelper::DrawField(const std::string& name, FieldType* value, PropertyWidget widget)
{
	if (widget == PropertyWidget::Const)
	{
		ImGui::Text("%s:", name.c_str());
		ImGui::SameLine();

		if constexpr (std::is_same_v<FieldType, std::string>)
			ImGui::TextUnformatted(value->c_str());
		else if constexpr (std::is_same_v<FieldType, bool>)
			ImGui::TextUnformatted(*value ? "true" : "false");
		else if constexpr (std::is_integral_v<FieldType> && std::is_signed_v<FieldType>)
			ImGui::Text("%lld", static_cast<long long>(*value));
		else if constexpr (std::is_integral_v<FieldType> && std::is_unsigned_v<FieldType>)
			ImGui::Text("%llu", static_cast<unsigned long long>(*value));
		else if constexpr (std::is_floating_point_v<FieldType>)
			ImGui::Text("%.6g", static_cast<double>(*value));
		else if constexpr (std::is_same_v<FieldType, glm::vec2>)
			ImGui::Text("(%.4g, %.4g)", static_cast<double>(value->x), static_cast<double>(value->y));
		else if constexpr (std::is_same_v<FieldType, glm::vec3>)
			ImGui::Text("(%.4g, %.4g, %.4g)", static_cast<double>(value->x), static_cast<double>(value->y), static_cast<double>(value->z));
		else if constexpr (std::is_same_v<FieldType, glm::vec4>)
			ImGui::Text("(%.4g, %.4g, %.4g, %.4g)", static_cast<double>(value->x), static_cast<double>(value->y), static_cast<double>(value->z), static_cast<double>(value->w));
		else if constexpr (std::is_enum_v<FieldType>)
			ImGui::Text("%lld", static_cast<long long>(static_cast<std::underlying_type_t<FieldType>>(*value)));
		else
			ImGui::TextUnformatted("<read-only>");

		return false;
	}

	if constexpr (std::is_same_v<FieldType, float>) {
		SetNextPropertyFieldWidth(130.0f, 0.55f);
		return ImGui::DragFloat(name.c_str(), value, 0.1f);
	}
	else if constexpr (std::is_same_v<FieldType, int>) {
		SetNextPropertyFieldWidth(130.0f, 0.55f);
		return ImGui::DragInt(name.c_str(), value, 1);
	}
	else if constexpr (std::is_same_v<FieldType, uint32_t>) {
		int temp = static_cast<int>(*value);
		SetNextPropertyFieldWidth(130.0f, 0.55f);
		if (ImGui::DragInt(name.c_str(), &temp, 1, 0, 1000000)) {
			if (temp >= 0) {
				*value = static_cast<uint32_t>(temp);
				return true;
			}
		}
		return false;
	}
	else if constexpr (std::is_same_v<FieldType, bool>) {
		return ImGui::Checkbox(name.c_str(), value);
	}
	else if constexpr (std::is_same_v<FieldType, std::string>) {
		char buffer[256];
		strcpy_s(buffer, value->c_str());
		SetNextPropertyFieldWidth(220.0f, 0.6f);
		if (ImGui::InputText(name.c_str(), buffer, sizeof(buffer))) {
			*value = buffer;
			return true;
		}
		return false;
	}
	else if constexpr (std::is_same_v<FieldType, glm::vec3>) {
		if (widget == PropertyWidget::Color) {
			// 颜色字段
			SetNextPropertyFieldWidth(250.0f, 0.6f);
			return ImGui::ColorEdit3(name.c_str(), glm::value_ptr(*value));
		}
		else {
			// 普通 vec3 默认处理
			SetNextPropertyFieldWidth(250.0f, 0.6f);
			return ImGui::DragFloat3(name.c_str(), glm::value_ptr(*value), 0.1f);
		}
	}
	else if constexpr (std::is_same_v<FieldType, glm::vec2>) {
		SetNextPropertyFieldWidth(180.0f, 0.55f);
		return ImGui::DragFloat2(name.c_str(), glm::value_ptr(*value), 0.1f);
	}
	else if constexpr (std::is_same_v<FieldType, glm::vec4>) {
		SetNextPropertyFieldWidth(280.0f, 0.65f);
		return ImGui::DragFloat4(name.c_str(), glm::value_ptr(*value), 0.1f);
	}
	else if constexpr (std::is_same_v<FieldType, glm::quat>) {
		// 欧拉角由面板记住，不每帧从四元数重算（见 EditorRotation）
		SetNextPropertyFieldWidth(250.0f, 0.6f);
		return EditorRotation::DragEuler(name.c_str(), *value, 1.0f, "%.1f");
	}
	else if constexpr (std::is_same_v<FieldType, ImColor>) {
		SetNextPropertyFieldWidth(180.0f, 0.55f);
		return ImGui::ColorEdit4(name.c_str(), (float*)value);
	}
	else {
		ImGui::Text("%s: [%s]", name.c_str(), typeid(FieldType).name());
		return false;
	}
}

template<>
inline bool PropertiesHelper::DrawField<Physics::BodyType>(const std::string& name, Physics::BodyType* value, PropertyWidget widget)
{
	int currentItem = static_cast<int>(*value);
	const char* items[] = { "Static", "Dynamic", "Kinematic" };
	SetNextPropertyFieldWidth(130.0f, 0.55f);
	if (ImGui::Combo(name.c_str(), &currentItem, items, IM_ARRAYSIZE(items))) {
		*value = static_cast<Physics::BodyType>(currentItem);
		return true;
	}
	return false;
}

template<>
inline bool PropertiesHelper::DrawField<Scene3DRenderOption::TransparentMode>(const std::string& name, Scene3DRenderOption::TransparentMode* value, PropertyWidget widget)
{
	int currentItem = static_cast<int>(*value);
	const char* items[] = { "Sorted", "OIT" };
	SetNextPropertyFieldWidth(250.0f, 0.55f);
	if (ImGui::Combo(name.c_str(), &currentItem, items, IM_ARRAYSIZE(items))) {
		*value = static_cast<Scene3DRenderOption::TransparentMode>(currentItem);
		return true;
	}
	return false;
}

template <typename T, typename = void>
struct has_reflector : std::false_type {};

template <typename T>
struct has_reflector<T, std::void_t<typename rfl::Reflector<T>::ReflType>>
	: std::true_type {};

template<typename T>
bool PropertiesHelper::DrawComponent(T& comp)
{
	bool anyChanged = false;

	if constexpr (has_reflector<T>::value) {
		using ReflType = typename rfl::Reflector<T>::ReflType;
		ReflType refl_obj = rfl::Reflector<T>::from(comp);// 从组件创建反射对象

		const auto fields = rfl::fields<ReflType>();// 获取字段信息

		if constexpr (std::tuple_size_v<decltype(fields)> > 0)
		{
			auto view = rfl::to_view(refl_obj);

			// 遍历所有字段并绘制
			[&] <std::size_t... I>(std::index_sequence<I...>) {
				((anyChanged |= [&]() -> bool {
					const std::string& field_name = std::get<I>(fields).name();
					auto* ptr = view.template get<I>();
					if constexpr (std::is_pointer_v<decltype(ptr)>) {
						using FieldType = std::remove_pointer_t<decltype(ptr)>;
						return DrawPropertyField<T>(field_name, ptr);
					}
					return false;
					}()), ...);
			}(std::make_index_sequence<fields.size()>{});

			// 如果有任何字段被修改，将反射对象写回组件
			if (anyChanged) {
				rfl::Reflector<T>::modify(comp, refl_obj);
			}

			return anyChanged;
		}
		else
		{
			return false;
		}

	}
	else {
		if constexpr (std::is_empty_v<T>)
		{
			return false;
		}
		else
		{
			const auto fields = rfl::fields<T>();

			if constexpr (std::tuple_size_v<decltype(fields)> > 0)
			{
				auto view = rfl::to_view(comp);

				[&] <std::size_t... I>(std::index_sequence<I...>) {
					((anyChanged |= [&]() -> bool {
						const std::string& field_name = std::get<I>(fields).name();
						auto* ptr = view.template get<I>();
						if constexpr (std::is_pointer_v<decltype(ptr)>) {
							using FieldType = std::remove_pointer_t<decltype(ptr)>;
							return DrawPropertyField<T>(field_name, ptr);
						}
						return false;
						}()), ...);
				}(std::make_index_sequence<fields.size()>{});
			}
			else
			{
				return false;
			}
		}
	}

	return anyChanged;
}

namespace PropertiesResetDetail
{
	template<typename V, typename = void>
	struct Comparable : std::false_type {};
	template<typename V>
	struct Comparable<V, std::void_t<decltype(std::declval<const V&>() != std::declval<const V&>())>> : std::true_type {};

	template<typename V>
	std::string FormatValue(const V& v)
	{
		char buf[128] = {};
		if constexpr (std::is_same_v<V, float> || std::is_same_v<V, double>)
			std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(v));
		else if constexpr (std::is_enum_v<V>)
			std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(static_cast<std::underlying_type_t<V>>(v)));
		else if constexpr (std::is_integral_v<V> && std::is_signed_v<V>)
			std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
		else if constexpr (std::is_integral_v<V>)
			std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(v));
		else if constexpr (std::is_same_v<V, glm::vec2>)
			std::snprintf(buf, sizeof(buf), "(%.4g, %.4g)", double(v.x), double(v.y));
		else if constexpr (std::is_same_v<V, glm::vec3>)
			std::snprintf(buf, sizeof(buf), "(%.4g, %.4g, %.4g)", double(v.x), double(v.y), double(v.z));
		else if constexpr (std::is_same_v<V, glm::vec4>)
			std::snprintf(buf, sizeof(buf), "(%.4g, %.4g, %.4g, %.4g)", double(v.x), double(v.y), double(v.z), double(v.w));
		else if constexpr (std::is_same_v<V, glm::uvec3>)
			std::snprintf(buf, sizeof(buf), "(%u, %u, %u)", v.x, v.y, v.z);
		else if constexpr (std::is_same_v<V, glm::ivec3>)
			std::snprintf(buf, sizeof(buf), "(%d, %d, %d)", v.x, v.y, v.z);
		else if constexpr (std::is_same_v<V, std::string>)
			return v;
		return buf;
	}
}

template<typename V>
bool PropertiesHelper::ResetButton(const char* id, V& value, const V& defaultValue)
{
	if constexpr (std::is_same_v<V, bool>)
	{
		return false;	// 开关不重置
	}
	else
	{
		if constexpr (PropertiesResetDetail::Comparable<V>::value)
		{
			if (!(value != defaultValue))
				return false;	// 已经是默认值，不显示按钮
		}
		ImGui::SameLine();
		ImGui::PushID(id);
		const bool clicked = ImGui::SmallButton("重置");
		ImGui::PopID();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("恢复默认值：%s", PropertiesResetDetail::FormatValue(defaultValue).c_str());
		if (!clicked)
			return false;
		value = defaultValue;
		return true;
	}
}

template<typename T>
bool PropertiesHelper::DrawDataResettable(T& data)
{
	// 自定义反射 / 空结构没有逐字段的默认值可比，退回普通绘制（手写面板的特化见下面 DDGIParams）
	if constexpr (has_reflector<T>::value || std::is_empty_v<T>)
	{
		return DrawComponent(data);
	}
	else
	{
		bool anyChanged = false;
		T defaults{};
		const auto fields = rfl::fields<T>();
		if constexpr (std::tuple_size_v<decltype(fields)> > 0)
		{
			auto view = rfl::to_view(data);
			auto defaultView = rfl::to_view(defaults);
			[&] <std::size_t... I>(std::index_sequence<I...>) {
				((anyChanged |= [&]() -> bool {
					const std::string& field_name = std::get<I>(fields).name();
					auto* ptr = view.template get<I>();
					auto* defaultPtr = defaultView.template get<I>();
					if constexpr (std::is_pointer_v<decltype(ptr)>) {
						bool c = DrawPropertyField<T>(field_name, ptr);
						if (PropertyUI<T>::GetWidget(field_name) != PropertyWidget::Const)
							c |= ResetButton(field_name.c_str(), *ptr, *defaultPtr);
						return c;
					}
					return false;
					}()), ...);
			}(std::make_index_sequence<fields.size()>{});
		}
		return anyChanged;
	}
}

template<typename T>
bool PropertiesHelper::ResetValues(T& data)
{
	// 整体赋默认值：带自定义反射的结构（如 DDGIParams）逐字段遍历不到，这样也能重置
	data = T{};
	return true;
}

// DDGI 参数面板。手写是为了在级联关闭时把级联参数置灰，并给出提示。
// 探针来源：跟随相机的级联（这里）+ 场景里的 DDGIVolumeComponent（实体组件），两者可以叠加。
template<>
inline bool PropertiesHelper::DrawComponent(Scene3DRenderOption::DDGIParams& p)
{
	bool changed = false;
	const Scene3DRenderOption::DDGIParams d{};		// 默认值（「重置」按钮用；开关不重置）

	// ── 跟随相机的级联 ──
	changed |= ImGui::Checkbox("cascadeEnable（跟随相机）", &p.cascadeEnable);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("以相机为中心的多级探针体积，间距逐级翻倍。\n关闭后只有场景里的 DDGI 体积生效；两者都没有时 DDGI 不输出");

	if (!p.cascadeEnable)
		ImGui::BeginDisabled();

	int levels = static_cast<int>(p.cascadeLevels);
	SetNextPropertyFieldWidth(130.0f, 0.55f);
	if (ImGui::SliderInt("cascadeLevels", &levels, 1, 4))
	{
		p.cascadeLevels = static_cast<uint32_t>(levels);
		changed = true;
	}
	changed |= ResetButton("cascadeLevels", p.cascadeLevels, d.cascadeLevels);

	SetNextPropertyFieldWidth(250.0f, 0.6f);
	if (ImGui::DragFloat3("cascadeBaseSpacing", glm::value_ptr(p.cascadeBaseSpacing), 0.05f, 0.25f, 16.0f))
	{
		p.cascadeBaseSpacing = glm::max(p.cascadeBaseSpacing, glm::vec3(0.25f));
		changed = true;
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("第 0 级的探针间距，之后每级翻倍");
	changed |= ResetButton("cascadeBaseSpacing", p.cascadeBaseSpacing, d.cascadeBaseSpacing);

	int counts[3] = { int(p.cascadeCounts.x), int(p.cascadeCounts.y), int(p.cascadeCounts.z) };
	SetNextPropertyFieldWidth(250.0f, 0.6f);
	if (ImGui::DragInt3("cascadeCounts", counts, 0.2f, 1, 255))
	{
		p.cascadeCounts = glm::uvec3(glm::clamp(glm::ivec3(counts[0], counts[1], counts[2]), glm::ivec3(1), glm::ivec3(255)));
		changed = true;
	}
	changed |= ResetButton("cascadeCounts", p.cascadeCounts, d.cascadeCounts);

	SetNextPropertyFieldWidth(130.0f, 0.55f);
	if (ImGui::DragFloat("cascadeFadeCells", &p.cascadeFadeCells, 0.05f, 0.01f, 8.0f))
	{
		p.cascadeFadeCells = std::max(0.01f, p.cascadeFadeCells);
		changed = true;
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("级与级之间（以及级联外沿）的过渡宽度，单位：格");
	changed |= ResetButton("cascadeFadeCells", p.cascadeFadeCells, d.cascadeFadeCells);
	changed |= ImGui::Checkbox("cascadeAutoRelocate", &p.cascadeAutoRelocate);

	{
		const unsigned long long perLevel =
			static_cast<unsigned long long>(p.cascadeCounts.x) * p.cascadeCounts.y * p.cascadeCounts.z;
		const glm::vec3 extent = glm::vec3(p.cascadeCounts) * p.cascadeBaseSpacing * float(1u << (std::max(1u, p.cascadeLevels) - 1u));
		ImGui::TextDisabled("%llu probes x %u 级，最外层 %.0f x %.0f x %.0f m",
			perLevel, p.cascadeLevels, static_cast<double>(extent.x), static_cast<double>(extent.y), static_cast<double>(extent.z));
		if (perLevel * p.cascadeLevels > GlobalConfig::DDGIProbeCapacity)
			ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f), "超过探针总容量 %u，外层级联会被停用", GlobalConfig::DDGIProbeCapacity);
	}

	if (!p.cascadeEnable)
		ImGui::EndDisabled();

	ImGui::Separator();

	// ── 着色 ──
	SetNextPropertyFieldWidth(130.0f, 0.55f);
	if (ImGui::DragFloat("normalBias", &p.normalBias, 0.005f, 0.0f, 0.5f))
		changed = true;
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("采样点沿法线外推，单位为所在体积最小间距的倍数（各级级联各自缩放），\n抑制自遮挡与薄墙漏光。建议 0.1~0.3，与 viewBias 之和不超过 0.5。");
	changed |= ResetButton("normalBias", p.normalBias, d.normalBias);
	SetNextPropertyFieldWidth(130.0f, 0.55f);
	if (ImGui::DragFloat("viewBias", &p.viewBias, 0.005f, 0.0f, 0.5f))
		changed = true;
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("采样点朝观察者方向外推，单位为所在体积最小间距的倍数（各级级联各自缩放），\n与 normalBias 配合抑制掠射角漏光。建议 0.1~0.3，与 normalBias 之和不超过 0.5。");
	changed |= ResetButton("viewBias", p.viewBias, d.viewBias);
	SetNextPropertyFieldWidth(130.0f, 0.55f);
	if (ImGui::DragFloat("maxRayDistance", &p.maxRayDistance, 0.5f, 0.5f, 500.0f))
		changed = true;
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("探针光线最远距离，超过视为看到天空。室外场景要调大");
	changed |= ResetButton("maxRayDistance", p.maxRayDistance, d.maxRayDistance);
	SetNextPropertyFieldWidth(130.0f, 0.55f);
	if (ImGui::DragFloat("backfaceThreshold", &p.backfaceThreshold, 0.05f, 0.001f, 8.0f))
		changed = true;
	changed |= ResetButton("backfaceThreshold", p.backfaceThreshold, d.backfaceThreshold);
	SetNextPropertyFieldWidth(130.0f, 0.55f);
	if (ImGui::DragFloat("skyIntensity", &p.skyIntensity, 0.02f, 0.0f, 8.0f))
		changed = true;
	changed |= ResetButton("skyIntensity", p.skyIntensity, d.skyIntensity);
	SetNextPropertyFieldWidth(130.0f, 0.55f);
	if (ImGui::DragFloat("energy", &p.energy, 0.02f, 0.0f, 8.0f))
		changed = true;
	changed |= ResetButton("energy", p.energy, d.energy);

	// ── 更新与时域 ──
	int budget = static_cast<int>(p.updateBudget);
	SetNextPropertyFieldWidth(130.0f, 0.55f);
	if (ImGui::DragInt("updateBudget", &budget, 16.0f, 64, static_cast<int>(GlobalConfig::DDGIUpdateBudget)))
	{
		p.updateBudget = static_cast<uint32_t>(std::clamp(budget, 64, static_cast<int>(GlobalConfig::DDGIUpdateBudget)));
		changed = true;
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("每帧最多更新的探针数（上限 GlobalConfig::DDGIUpdateBudget = %u）。\n按距离分给各体积：近处更新勤、远处慢；新出现的探针优先", GlobalConfig::DDGIUpdateBudget);
	changed |= ResetButton("updateBudget", p.updateBudget, d.updateBudget);

	SetNextPropertyFieldWidth(130.0f, 0.55f);
	if (ImGui::DragFloat("hysteresis", &p.hysteresis, 0.005f, 0.0f, 0.98f))
		changed = true;
	changed |= ResetButton("hysteresis", p.hysteresis, d.hysteresis);
	SetNextPropertyFieldWidth(130.0f, 0.55f);
	if (ImGui::DragFloat("lightChangeThreshold", &p.lightChangeThreshold, 0.01f, 0.0f, 2.0f))
		changed = true;
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("探针平均亮度的相对变化超过该值（且超出跨帧噪声）时临时降低 hysteresis，\n开关灯时快速跟上。0 = 关闭。");
	changed |= ResetButton("lightChangeThreshold", p.lightChangeThreshold, d.lightChangeThreshold);

	return changed;
}

// DDGI 面板是手写的（级联置灰 / 提示），「重置」按钮直接写在上面的 DrawComponent 里
template<>
inline bool PropertiesHelper::DrawDataResettable(Scene3DRenderOption::DDGIParams& p)
{
	return DrawComponent(p);
}

// 物理组件：手写面板。
//   布局：类型（分段按钮，点一下就切换）→ 刚体 / 角色参数 → 选项 → 碰撞形状
//   - 只在某种模式下有意义的参数：换模式就不显示；只对某种物体类型生效的：变灰，悬停说明原因（对照 PhysicsSystem）
//   - 会导致物理体建不出来、场景存不下的配置，集中提示在碰撞形状上方，并给一键修复按钮
//   - 碰撞形状：列表（固定高度、可拖动调高、只画看得见的行，和材质槽一样）+ 选中形状的参数；
//     右键行复制、移动、删除；悬停的行在视口里高亮；基本形状可以一键包住模型；从模型生成包围盒 / 三角网格 / 凸包 / 凸分解
//   改的时候先复制再改（写时复制），提交时形状变了会重建物理体（见 EditorSnapshot<Physics>）
template<>
inline bool PropertiesHelper::DrawComponent<Physics>(Physics& physics)
{
	using ChildShape = CollisionShape::ChildShape;
	bool changed = false;
	const ImGuiStyle& style = ImGui::GetStyle();
	const float frameHeight = ImGui::GetFrameHeight();
	const ImVec4 warnColor(1.0f, 0.75f, 0.3f, 1.0f);
	const float labelWidth = ImGui::CalcTextSize("Max Penetration").x + style.ItemSpacing.x * 2.0f;
	constexpr ImGuiSliderFlags kClamp = ImGuiSliderFlags_AlwaysClamp;

	const Entity entity = s_drawingEntity;
	const bool hasModel = PhysicsShapeFromModel::HasStaticModel(entity);
	auto& children = physics.collisionShape.children;
	const auto countTriangleMeshes = [&]
		{
			return std::any_of(children.begin(), children.end(), [](const auto& c) { return c && c->shapeType == ShapeType::TriangleMesh; });
		};

	const auto beginTable = [&](const char* id) -> bool
		{
			if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit))
				return false;
			ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
			ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
			return true;
		};
	// 一行：左标签（可带悬停说明），进入右列，控件宽度占满
	const auto row = [](const char* label, const char* tooltip = nullptr)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			if (tooltip && ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", tooltip);
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-FLT_MIN);
		};
	// 在当前情况下不生效的控件：变灰，悬停说明原因
	const auto disabledIf = [](bool disabled, const char* reason, auto&& draw) -> bool
		{
			if (disabled)
				ImGui::BeginDisabled();
			const bool edited = draw();
			if (disabled)
			{
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("%s", reason);
			}
			return edited;
		};
	// 分段按钮：一排等宽按钮，当前项高亮，点一下就切换。返回点中的项（没点 / 点的是当前项返回 -1）
	const auto segmented = [&](const char* id, int current, int count, const char* const* items, const char* const* tips, const bool* disabled) -> int
		{
			int clicked = -1;
			ImGui::PushID(id);
			const float spacing = style.ItemInnerSpacing.x;
			const float width = std::max(1.0f, (ImGui::GetContentRegionAvail().x - spacing * static_cast<float>(count - 1)) / static_cast<float>(count));
			for (int i = 0; i < count; ++i)
			{
				if (i > 0)
					ImGui::SameLine(0.0f, spacing);
				const bool active = i == current;
				const bool off = disabled && disabled[i] && !active;
				if (active)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
				}
				if (off)
					ImGui::BeginDisabled();
				if (ImGui::Button(items[i], ImVec2(width, 0.0f)) && !active)
					clicked = i;
				if (off)
					ImGui::EndDisabled();
				if (active)
					ImGui::PopStyleColor(2);
				if (tips && tips[i] && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("%s", tips[i]);
			}
			ImGui::PopID();
			return clicked;
		};

	// 按模型包围盒算一个基本形状（在场景线程上读模型；编辑时场景没在跑，命令直接执行）。读不到返回空
	const auto fitToModel = [&](ShapeType type) -> std::shared_ptr<ChildShape>
		{
			glm::vec3 mn(0.0f), mx(0.0f);
			std::string error;
			bool ok = false;
			if (World* world = entity ? entity.getWorld() : nullptr)
				world->SubmitCommand([&](World& w) { ok = PhysicsShapeFromModel::GetModelBounds(w, entity, mn, mx, error); }).get();
			if (!ok)
				return nullptr;
			auto shape = std::make_shared<ChildShape>();
			shape->shapeType = type;
			PhysicsShapeFromModel::FitPrimitive(type, mn, mx, shape->size, shape->position);
			return shape;
		};
	// 新的基本形状：fit 且有模型时包住模型，否则用默认尺寸
	const auto makeShape = [&](ShapeType type, bool fit) -> std::shared_ptr<ChildShape>
		{
			if (fit && hasModel)
			{
				if (auto fitted = fitToModel(type))
					return fitted;
			}
			CollisionShape temp;
			switch (type)
			{
			case ShapeType::Sphere: temp.AddSphereShape(); break;
			case ShapeType::Capsule: temp.AddCapsuleShape(); break;
			case ShapeType::Cylinder: temp.AddCylinderShape(); break;
			default: temp.AddBoxShape(); break;
			}
			return temp.children.front();
		};

	// ======================== 类型 ========================
	if (beginTable("##PhysicsBody"))
	{
		const char* modes[] = { "Rigid Body", "Character" };
		const char* modeTips[] = {
			"普通刚体：按质量 / 摩擦 / 弹性参与物理模拟",
			"Bullet 角色控制器：用 Movement 组件的输入行走、跳跃；只用第一个碰撞形状" };
		row("Mode");
		if (const int m = segmented("##Mode", physics.isCharacter ? 1 : 0, 2, modes, modeTips, nullptr); m >= 0)
		{
			physics.isCharacter = m == 1;
			changed = true;
		}

		row("Body Type");
		if (!physics.isCharacter)
		{
			// 有三角网格时只能 Static（Bullet 的三角网格不能动；SceneCodec 保存时也会校验），别的类型点不了
			const bool meshOnly = countTriangleMeshes();
			const char* meshReason = "有三角网格形状，只能是 Static（先删掉三角网格，或者换成凸包 / 凸分解）";
			const char* types[] = { "Static", "Dynamic", "Kinematic" };
			const char* tips[] = {
				"不动，质量按 0 处理（地面、墙）",
				meshOnly ? meshReason : "受重力和碰撞影响，由物理模拟驱动 Transform",
				meshOnly ? meshReason : "不受力，每帧跟随 Transform 移动，能推开 Dynamic 物体（移动平台、门）" };
			const bool disabled[] = { false, meshOnly, meshOnly };
			if (const int t = segmented("##BodyType", static_cast<int>(physics.bodyType), 3, types, tips, disabled); t >= 0)
			{
				physics.bodyType = static_cast<Physics::BodyType>(t);
				changed = true;
			}
		}
		else
		{
			// 角色控制器只看是不是 Kinematic（PhysicsSystem::createCharacter），Static / Dynamic 没有区别
			const char* types[] = { "Normal", "Kinematic" };
			const char* tips[] = {
				"普通角色：受重力，按 Movement 的输入移动",
				"加上 Kinematic 标记，并且始终保持激活（不休眠）" };
			const bool kinematic = physics.bodyType == Physics::BodyType::Kinematic;
			if (const int t = segmented("##CharacterType", kinematic ? 1 : 0, 2, types, tips, nullptr); t >= 0)
			{
				physics.bodyType = t == 1 ? Physics::BodyType::Kinematic : Physics::BodyType::Dynamic;
				changed = true;
			}
		}
		ImGui::EndTable();
	}

	const bool isStatic = physics.bodyType == Physics::BodyType::Static;
	const bool isDynamic = physics.bodyType == Physics::BodyType::Dynamic;
	const bool isKinematic = physics.bodyType == Physics::BodyType::Kinematic;

	// ======================== 刚体 / 角色参数 ========================
	if (!physics.isCharacter)
	{
		ImGui::SeparatorText("Rigid Body");
		if (beginTable("##PhysicsRigid"))
		{
			row("Mass", "单位 kg；只对 Dynamic 生效");
			changed |= disabledIf(!isDynamic, "Static / Kinematic 的质量固定按 0（不受力）处理",
				[&] { return ImGui::DragFloat("##Mass", &physics.mass, 0.05f, 0.001f, 1.0e6f, "%.3f kg", kClamp); });
			row("Friction", "滑动摩擦系数（0 = 冰面）");
			changed |= ImGui::DragFloat("##Friction", &physics.friction, 0.01f, 0.0f, 2.0f, "%.2f", kClamp);
			row("Rolling Friction", "滚动摩擦（球、圆柱滚动时的阻力）");
			changed |= ImGui::DragFloat("##RollingFriction", &physics.rollingFriction, 0.01f, 0.0f, 1.0f, "%.2f", kClamp);
			row("Restitution", "弹性：0 = 不反弹，1 = 完全弹回");
			changed |= ImGui::SliderFloat("##Restitution", &physics.restitution, 0.0f, 1.0f, "%.2f", kClamp);
			ImGui::EndTable();
		}
	}
	else
	{
		ImGui::SeparatorText("Character");
		if (beginTable("##PhysicsCharacter"))
		{
			row("Walk Speed", "Movement 输入方向乘上它，单位 m/s");
			changed |= ImGui::DragFloat("##WalkSpeed", &physics.walkSpeed, 0.1f, 0.0f, 1000.0f, "%.2f m/s", kClamp);
			row("Jump Speed", "起跳速度，单位 m/s");
			changed |= ImGui::DragFloat("##JumpSpeed", &physics.jumpSpeed, 0.1f, 0.0f, 1000.0f, "%.2f m/s", kClamp);
			row("Step Height", "能直接走上去的台阶高度");
			changed |= ImGui::DragFloat("##StepHeight", &physics.stepHeight, 0.01f, 0.0f, 10.0f, "%.2f m", kClamp);
			row("Max Slope", "能走上去的最大坡度");
			changed |= ImGui::SliderFloat("##MaxSlope", &physics.maxSlope, 0.0f, 89.0f, "%.0f°", kClamp);
			row("Max Penetration", "允许嵌进其他物体的最大深度，太小容易抖，太大会穿模");
			changed |= ImGui::DragFloat("##MaxPenetration", &physics.maxPenetrationDepth, 0.005f, 0.0f, 1.0f, "%.3f m", kClamp);
			ImGui::EndTable();
		}
	}

	// ======================== 选项：两列勾选框 ========================
	// 角色控制器用不到的（锁定旋转、CCD）不显示；只对某种物体类型生效的变灰
	ImGui::SeparatorText("Options");
	if (ImGui::BeginTable("##PhysicsOptions", 2, ImGuiTableFlags_SizingStretchSame))
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		changed |= ImGui::Checkbox("Sensor", &physics.isSensor);
		ImGui::SetItemTooltip("只检测重叠、不产生碰撞响应（触发区域）");
		ImGui::TableSetColumnIndex(1);
		changed |= disabledIf(isKinematic, "Kinematic 始终保持激活",
			[&] { return ImGui::Checkbox("Allow Sleep", &physics.allowSleep); });
		if (!isKinematic)
			ImGui::SetItemTooltip("静止一段时间后休眠，不参与模拟（省性能）");

		if (!physics.isCharacter)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			changed |= disabledIf(!isDynamic, "只对 Dynamic 刚体生效",
				[&] { return ImGui::Checkbox("Fixed Rotation", &physics.fixedRotation); });
			if (isDynamic)
				ImGui::SetItemTooltip("锁定旋转：碰撞不会让它翻倒");
			ImGui::TableSetColumnIndex(1);
			changed |= disabledIf(isStatic, "Static 物体不动，用不到连续碰撞检测",
				[&] { return ImGui::Checkbox("Continuous (CCD)", &physics.isBullet); });
			if (!isStatic)
				ImGui::SetItemTooltip("连续碰撞检测：高速物体（子弹）不会穿过薄物体");
		}
		ImGui::EndTable();
	}

	// ======================== 碰撞形状 ========================
	ImGui::SeparatorText(std::format("Collision Shapes ({})", children.size()).c_str());

	// 每个实体（ImGui ID 栈里有实体 id）一份列表状态：选中第几个形状、已经滚到哪一行
	struct ShapePanelState
	{
		int selected = 0;
		int scrolledTo = -1;
	};
	static std::unordered_map<ImGuiID, ShapePanelState> s_shapePanels;
	ShapePanelState& shapePanel = s_shapePanels[ImGui::GetID("##physicsShapes")];
	if (shapePanel.selected >= static_cast<int>(children.size()))
		shapePanel.selected = children.empty() ? -1 : static_cast<int>(children.size()) - 1;
	if (shapePanel.selected < 0 && !children.empty())
		shapePanel.selected = 0;
	// 加了形状以后选中新加的那个
	const auto selectShape = [&](size_t index) { shapePanel.selected = static_cast<int>(index); };

	const char* shapeNames[] = { "Box", "Sphere", "Capsule", "Cylinder", "Convex Hull", "Triangle Mesh" };
	const auto shapeName = [&](ShapeType type) { return shapeNames[std::clamp(static_cast<int>(type), 0, 5)]; };
	const auto isPrimitive = [](ShapeType type) { return type == ShapeType::Box || type == ShapeType::Sphere || type == ShapeType::Capsule || type == ShapeType::Cylinder; };

	// 不同类型之间切换时，按"半径 + 总高"换算尺寸，形状大小大致不变
	const auto convertSize = [](ShapeType from, ShapeType to, const glm::vec3& size) -> glm::vec3
		{
			float radius = size.x;
			float height = size.y * 2.0f;
			switch (from)
			{
			case ShapeType::Box: radius = std::max(size.x, size.z); height = size.y * 2.0f; break;
			case ShapeType::Sphere: radius = size.x; height = size.x * 2.0f; break;
			case ShapeType::Capsule: radius = size.x; height = size.y + size.x * 2.0f; break;
			case ShapeType::Cylinder: radius = size.x; height = size.y * 2.0f; break;
			default: break;
			}
			radius = std::max(radius, 0.001f);
			height = std::max(height, 0.001f);
			switch (to)
			{
			case ShapeType::Box: return glm::vec3(radius, height * 0.5f, radius);
			case ShapeType::Sphere: return glm::vec3(radius);
			case ShapeType::Capsule: return glm::vec3(radius, std::max(0.0f, height - radius * 2.0f), radius);
			case ShapeType::Cylinder: return glm::vec3(radius, height * 0.5f, radius);
			default: return size;
			}
		};
	// 尺寸摘要（标题行上显示）
	const auto sizeSummary = [](const ChildShape& shape) -> std::string
		{
			switch (shape.shapeType)
			{
			case ShapeType::Box: return std::format("{:.2f} × {:.2f} × {:.2f}", shape.size.x * 2.0f, shape.size.y * 2.0f, shape.size.z * 2.0f);
			case ShapeType::Sphere: return std::format("r {:.2f}", shape.size.x);
			case ShapeType::Capsule: return std::format("r {:.2f} · h {:.2f}", shape.size.x, shape.size.y);
			case ShapeType::Cylinder: return std::format("r {:.2f} · h {:.2f}", shape.size.x, shape.size.y * 2.0f);
			case ShapeType::ConvexHull: return std::format("{} 个顶点", shape.vertices.size());
			case ShapeType::TriangleMesh: return std::format("{} 个三角形", shape.indices.size() / 3);
			default: return {};
			}
		};

	// ---- 问题提示 + 一键修复：fixLabel 非空时在提示下面画一个小按钮，点了返回 true ----
	const auto problem = [&](const char* id, const char* text, const char* fixLabel) -> bool
		{
			ImGui::PushID(id);
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextColored(warnColor, "%s", text);
			ImGui::PopTextWrapPos();
			bool clicked = false;
			if (fixLabel)
			{
				ImGui::Indent();
				clicked = ImGui::SmallButton(fixLabel);
				ImGui::Unindent();
			}
			ImGui::PopID();
			return clicked;
		};

	if (children.empty())
	{
		const bool character = physics.isCharacter;
		const char* fix = character ? (hasModel ? "按模型添加胶囊" : "添加胶囊")
			: (hasModel ? "按模型包围盒添加 Box" : "添加 Box");
		if (problem("##empty", "没有碰撞形状：不会创建物理体", fix))
		{
			children.push_back(makeShape(character ? ShapeType::Capsule : ShapeType::Box, true));
			selectShape(children.size() - 1);
			changed = true;
		}
	}
	else if (physics.isCharacter)
	{
		const ChildShape* first = children.front().get();
		if (first && first->shapeType == ShapeType::TriangleMesh)
		{
			if (problem("##charConvex", "角色控制器的第一个形状必须是凸形状（基本形状或凸包），现在是三角网格：不会创建角色",
				hasModel ? "在最前面加一个按模型的胶囊" : "在最前面加一个胶囊"))
			{
				children.insert(children.begin(), makeShape(ShapeType::Capsule, true));
				selectShape(0);
				changed = true;
			}
		}
		else if (first && (glm::length(first->position) > 1e-5f || !EditorRotation::SameRotation(first->rotation, glm::identity<glm::quat>())))
		{
			if (problem("##charOffset", "角色控制器忽略形状的偏移和旋转：看到的位置和实际碰撞对不上", "清零偏移和旋转"))
			{
				auto copy = std::make_shared<ChildShape>(*first);
				copy->position = glm::vec3(0.0f);
				copy->rotation = glm::identity<glm::quat>();
				children.front() = std::move(copy);
				changed = true;
			}
		}
		if (entity && !entity.tryGetComponent<Movement>())
			problem("##charMove", "没有 Movement 组件：角色不会走动和跳跃", nullptr);
		if (children.size() > 1)
			ImGui::TextDisabled("角色控制器只用第一个形状（#0），其余（灰色）忽略");
	}
	if (!physics.isCharacter && !isStatic && countTriangleMeshes())
	{
		if (problem("##meshStatic", "三角网格只能用在 Static 物体上：物理会出错，保存场景也会失败", "改为 Static"))
		{
			physics.bodyType = Physics::BodyType::Static;
			changed = true;
		}
	}

	// ---- 工具栏：[+ 形状] [从模型...] [视口显示] [清空] ----
	struct GenerateStatus
	{
		EntityID entity = 0;
		bool valid = false;
		bool ok = false;
		std::string message;
	};
	static GenerateStatus s_generateStatus;
	static bool s_replaceShapes = true;		// 从模型生成：替换现有形状还是追加
	static bool s_fitNewShapes = true;		// 添加基本形状：有模型时包住模型
	static PhysicsShapeFromModel::DecompositionParams s_decompParams;
	static int s_resolutionLevel = 1;
	bool openDecompose = false;

	if (ImGui::Button("+ 形状"))
		ImGui::OpenPopup("##AddShape");
	ImGui::SetItemTooltip("添加基本形状（Box / Sphere / Capsule / Cylinder）");
	ImGui::SameLine();
	if (!hasModel)
		ImGui::BeginDisabled();
	if (ImGui::Button("从模型..."))
		ImGui::OpenPopup("##ShapeFromModel");
	if (!hasModel)
		ImGui::EndDisabled();
	ImGui::SetItemTooltip(hasModel ? "按模型生成包围盒、三角网格、凸包，或者做凸分解"
		: "需要同一实体上有静态模型（Static Model）组件，并且模型已加载");
	ImGui::SameLine();
	{
		bool show = PhysicsDebugView::GetMode() != PhysicsDebugView::Mode::Off;
		if (ImGui::Checkbox("视口显示", &show))
			PhysicsDebugView::SetMode(show ? PhysicsDebugView::Mode::Selected : PhysicsDebugView::Mode::Off);
		ImGui::SetItemTooltip("在场景视口里显示碰撞体（同视口工具栏的「碰撞体」，勾上时显示选中的实体）。\n"
			"不勾时，鼠标悬停在下面的形状上也会临时显示；悬停的形状画成白色");
	}
	ImGui::SameLine();
	if (children.empty())
		ImGui::BeginDisabled();
	if (ImGui::Button("清空"))
		ImGui::OpenPopup("##ClearShapes");
	if (children.empty())
		ImGui::EndDisabled();

	if (ImGui::BeginPopup("##ClearShapes"))
	{
		ImGui::Text("删除全部 %d 个碰撞形状？", static_cast<int>(children.size()));
		ImGui::TextDisabled("编辑器没有撤销");
		if (ImGui::Button("删除"))
		{
			children.clear();
			changed = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("取消"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopup("##AddShape"))
	{
		if (!hasModel)
			ImGui::BeginDisabled();
		ImGui::Checkbox("包住模型", &s_fitNewShapes);
		if (!hasModel)
			ImGui::EndDisabled();
		ImGui::SetItemTooltip(hasModel ? "按模型的包围盒设置尺寸和偏移；不勾用默认尺寸" : "同一实体上没有静态模型，用默认尺寸");
		ImGui::Separator();
		for (const ShapeType type : { ShapeType::Box, ShapeType::Sphere, ShapeType::Capsule, ShapeType::Cylinder })
		{
			if (ImGui::MenuItem(shapeName(type)))
			{
				children.push_back(makeShape(type, s_fitNewShapes));
				selectShape(children.size() - 1);
				changed = true;
			}
		}
		ImGui::EndPopup();
	}

	const bool decompRunning = PhysicsShapeFromModel::Decomposition::IsRunning();
	if (ImGui::BeginPopup("##ShapeFromModel"))
	{
		using PhysicsShapeFromModel::Kind;
		std::optional<Kind> kind;
		const bool character = physics.isCharacter;
		ImGui::Checkbox("替换现有形状", &s_replaceShapes);
		ImGui::SetItemTooltip("勾上：删掉现有的碰撞形状，换成生成的；不勾：追加在后面");
		if (decompRunning)
			ImGui::TextColored(warnColor, "凸分解正在运行，完成后再生成");
		ImGui::Separator();
		if (decompRunning)
			ImGui::BeginDisabled();

		if (ImGui::MenuItem("包围盒（Box）"))
			kind = Kind::BoundingBox;
		ImGui::SetItemTooltip("模型包围盒的 Box：最便宜，任何物体类型都能用");
		if (ImGui::MenuItem("三角网格（Static）", nullptr, false, !character))
			kind = Kind::TriangleMesh;
		ImGui::SetItemTooltip(character
			? "角色控制器不能用三角网格"
			: "精确贴合模型表面，适合地形、建筑、场景里不动的物体。\n"
			  "只能用在 Static 物体上：生成后物体类型会改成 Static。\n"
			  "会焊接重复顶点、去掉退化三角形");
		if (ImGui::MenuItem("凸包（整个模型）"))
			kind = Kind::ConvexHull;
		ImGui::SetItemTooltip("把整个模型包成一个凸多面体，Dynamic / Kinematic 也能用。\n"
			"凹进去的部分会被填平；顶点超过 100 个时自动简化");
		if (ImGui::MenuItem("凸包（每个子网格一个）", nullptr, false, !character))
			kind = Kind::ConvexHullPerMesh;
		ImGui::SetItemTooltip(character
			? "角色控制器只用第一个形状"
			: "每个子网格各包一个凸包，比整个模型一个凸包更贴合凹形模型。\n"
			  "子网格本身凹的部分仍会被填平");
		ImGui::Separator();
		if (ImGui::MenuItem("凸分解（多个凸包）...", nullptr, false, !character))
			openDecompose = true;
		ImGui::SetItemTooltip(character
			? "角色控制器只用第一个形状，不能用凸分解"
			: "用 V-HACD 把凹形模型拆成多个凸包，Dynamic / Kinematic 也能贴合凹形模型。\n"
			  "在后台计算，可以设置凸包数量和精度");

		if (decompRunning)
			ImGui::EndDisabled();

		if (kind && entity)
		{
			PhysicsShapeFromModel::Result result;
			if (World* world = entity.getWorld())
				world->SubmitCommand([&](World& w) { result = PhysicsShapeFromModel::Build(w, entity, *kind); }).get();
			else
				result.message = "实体已经不存在";

			if (result.ok)
			{
				if (s_replaceShapes)
					children.clear();
				selectShape(children.size());		// 选中生成的第一个
				for (auto& shape : result.shapes)
					children.push_back(std::move(shape));
				if (*kind == Kind::TriangleMesh && physics.bodyType != Physics::BodyType::Static)
				{
					physics.bodyType = Physics::BodyType::Static;
					result.message += "；物体类型已改为 Static";
				}
				changed = true;
			}
			s_generateStatus = { entity.getId(), true, result.ok, std::move(result.message) };
		}
		ImGui::EndPopup();
	}

	// 凸分解参数（从「从模型...」菜单打开；和它在同一层 ID 栈里打开、绘制）
	if (openDecompose)
		ImGui::OpenPopup("##Decompose");
	if (ImGui::BeginPopup("##Decompose"))
	{
		static constexpr uint32_t kResolutions[] = { 50000, 100000, 400000, 1000000 };
		static constexpr const char* kResolutionNames[] = { "低（5 万体素）", "中（10 万）", "高（40 万）", "很高（100 万，很慢）" };
		const float fieldWidth = ImGui::GetFontSize() * 10.0f;

		ImGui::TextUnformatted("凸分解（V-HACD）");
		ImGui::Separator();
		int maxHulls = static_cast<int>(s_decompParams.maxHulls);
		int maxVertices = static_cast<int>(s_decompParams.maxVerticesPerHull);
		ImGui::SetNextItemWidth(fieldWidth);
		if (ImGui::SliderInt("最多凸包数", &maxHulls, 1, 64, "%d", kClamp))
			s_decompParams.maxHulls = static_cast<uint32_t>(maxHulls);
		ImGui::SetItemTooltip("凸包越多越贴合，碰撞检测也越慢。一般物体 8 ~ 32 个就够");
		ImGui::SetNextItemWidth(fieldWidth);
		if (ImGui::Combo("精度", &s_resolutionLevel, kResolutionNames, IM_ARRAYSIZE(kResolutionNames)))
			s_decompParams.resolution = kResolutions[std::clamp(s_resolutionLevel, 0, 3)];
		ImGui::SetItemTooltip("体素化的精细程度：越高越能保留细节（细杆、薄壁），计算也越慢");
		ImGui::SetNextItemWidth(fieldWidth);
		if (ImGui::SliderInt("每个凸包顶点", &maxVertices, 8, 128, "%d", kClamp))
			s_decompParams.maxVerticesPerHull = static_cast<uint32_t>(maxVertices);
		ImGui::SetItemTooltip("每个凸包最多几个顶点。Bullet 建议凸包顶点不要太多（64 左右）");
		ImGui::SetNextItemWidth(fieldWidth);
		ImGui::Combo("填充方式", &s_decompParams.fillMode, "洪水填充（默认）\0光线投射（模型不封闭）\0只取表面（空心）\0");
		ImGui::SetItemTooltip("体素化后怎么判断哪里是物体内部：\n"
			"洪水填充：默认，模型有破洞时可能变成空心；\n"
			"光线投射：模型不封闭（有破洞、缺面）时用；\n"
			"只取表面：只保留外壳，内部是空的");
		ImGui::Checkbox("替换现有形状", &s_replaceShapes);
		ImGui::Spacing();

		if (decompRunning)
			ImGui::BeginDisabled();
		if (ImGui::Button("开始分解", ImVec2(fieldWidth, 0.0f)) && entity)
		{
			PhysicsShapeFromModel::MeshData mesh;
			std::string error;
			bool read = false;
			World* world = entity.getWorld();
			if (world)
				world->SubmitCommand([&](World& w) { read = PhysicsShapeFromModel::ReadWeldedMesh(w, entity, mesh, error); }).get();
			else
				error = "实体已经不存在";
			if (!read || !PhysicsShapeFromModel::Decomposition::Start(world, entity, std::move(mesh), s_decompParams, s_replaceShapes))
				s_generateStatus = { entity.getId(), true, false, error.empty() ? std::string("无法开始分解") : error };
			else
				s_generateStatus.valid = false;		// 改为显示分解进度 / 结果
			ImGui::CloseCurrentPopup();
		}
		if (decompRunning)
		{
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("已经有一个分解任务在运行");
		}
		ImGui::SameLine();
		if (ImGui::Button("取消"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	// ---- 生成 / 分解的结果（只在同一个实体上显示）----
	if (const auto decomp = PhysicsShapeFromModel::Decomposition::GetStatus();
		decomp.valid && entity && decomp.entity == entity && !s_generateStatus.valid)
	{
		if (decomp.running)
		{
			ImGui::ProgressBar(decomp.progress, ImVec2(-FLT_MIN - frameHeight * 2.5f, 0.0f),
				std::format("凸分解 {:.0f}% {}", decomp.progress * 100.0f, decomp.stage).c_str());
			ImGui::SameLine();
			if (ImGui::Button("取消##decomp"))
				PhysicsShapeFromModel::Decomposition::Cancel();
		}
		else if (!decomp.message.empty())
		{
			ImGui::PushTextWrapPos(0.0f);
			if (decomp.ok)
				ImGui::TextDisabled("%s", decomp.message.c_str());
			else
				ImGui::TextColored(warnColor, "凸分解没有完成：%s", decomp.message.c_str());
			ImGui::PopTextWrapPos();
		}
	}
	if (s_generateStatus.valid && entity && s_generateStatus.entity == entity.getId())
	{
		ImGui::PushTextWrapPos(0.0f);
		if (s_generateStatus.ok)
			ImGui::TextDisabled("%s", s_generateStatus.message.c_str());
		else
			ImGui::TextColored(warnColor, "生成失败：%s", s_generateStatus.message.c_str());
		ImGui::PopTextWrapPos();
	}

	// ---- 形状列表：固定高度、可拖动调高、只画看得见的行（和材质槽列表一样）；单击选中，下面编辑选中的那个 ----
	// 行：#序号 类型 尺寸摘要                      标签（角色忽略 / 三角网格不是 Static 等）
	// 右键行：复制、上移、下移、移到最前、删除。悬停的行在视口里画成白色；没悬停时高亮选中的形状
	enum class ShapeAction { None, Duplicate, MoveUp, MoveDown, MoveFirst, Remove };
	ShapeAction action = ShapeAction::None;
	size_t actionIndex = 0;
	int hoveredRow = -1;

	if (!children.empty())
	{
		const float rowHeight = frameHeight;
		constexpr float kRowSpacing = 1.0f;
		const float rowStride = rowHeight + kRowSpacing;
		const size_t visibleRows = std::clamp<size_t>(children.size(), 1, 8);
		const float listHeight = visibleRows * rowStride - kRowSpacing + style.WindowPadding.y * 2.0f;
		ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, rowStride + style.WindowPadding.y * 2.0f), ImVec2(FLT_MAX, FLT_MAX));
		if (ImGui::BeginChild("##ShapeList", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, kRowSpacing));
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(children.size()), rowStride);
			if (shapePanel.selected >= 0 && shapePanel.scrolledTo != shapePanel.selected)
				clipper.IncludeItemByIndex(shapePanel.selected);
			while (clipper.Step())
			{
				for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r)
				{
					const size_t i = static_cast<size_t>(r);
					ImGui::PushID(r);
					const ChildShape* shape = children[i].get();
					const bool selected = shapePanel.selected == r;
					const ImVec2 p0 = ImGui::GetCursorScreenPos();
					const float rowWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
					const ImVec2 p1(p0.x + rowWidth, p0.y + rowHeight);

					ImGui::InvisibleButton("##row", ImVec2(rowWidth, rowHeight));
					const bool rowHovered = ImGui::IsItemHovered();
					if (rowHovered)
						hoveredRow = r;
					if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
					{
						shapePanel.selected = r;
						shapePanel.scrolledTo = r;		// 自己点的不用再滚
					}
					if (ImGui::BeginPopupContextItem("##ShapeMenu"))
					{
						hoveredRow = r;
						const auto pick = [&](ShapeAction a) { action = a; actionIndex = i; };
						if (ImGui::MenuItem("复制"))
							pick(ShapeAction::Duplicate);
						if (ImGui::MenuItem("上移", nullptr, false, i > 0))
							pick(ShapeAction::MoveUp);
						if (ImGui::MenuItem("下移", nullptr, false, i + 1 < children.size()))
							pick(ShapeAction::MoveDown);
						if (ImGui::MenuItem("移到最前", nullptr, false, i > 0))
							pick(ShapeAction::MoveFirst);
						if (physics.isCharacter)
							ImGui::SetItemTooltip("角色控制器只用第一个形状");
						ImGui::Separator();
						if (ImGui::MenuItem("删除"))
							pick(ShapeAction::Remove);
						ImGui::EndPopup();
					}

					ImDrawList* drawList = ImGui::GetWindowDrawList();
					if (selected || rowHovered)
						drawList->AddRectFilled(p0, p1, ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered), style.FrameRounding);

					if (shape)
					{
						const float textY = p0.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
						const float textX = p0.x + style.FramePadding.x;
						const bool ignored = physics.isCharacter && i > 0;
						const char* tag = ignored ? "忽略"
							: (shape->shapeType == ShapeType::TriangleMesh && !isStatic && !physics.isCharacter) ? "需要 Static"
							: (physics.isCharacter && i == 0 && shape->shapeType == ShapeType::TriangleMesh) ? "不能用"
							: nullptr;
						const float tagWidth = tag ? ImGui::CalcTextSize(tag).x : 0.0f;
						const float tagX = p1.x - style.FramePadding.x - tagWidth;
						if (tag)
							drawList->AddText(ImVec2(tagX, textY), ignored ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : ImGui::ColorConvertFloat4ToU32(warnColor), tag);

						const float nameRight = tag ? tagX - style.ItemSpacing.x : p1.x - style.FramePadding.x;
						const std::string index = std::format("#{}", i);
						const std::string name = std::format("{}  {}", shapeName(shape->shapeType), sizeSummary(*shape));
						drawList->PushClipRect(ImVec2(textX, p0.y), ImVec2(std::max(textX, nameRight), p1.y), true);
						drawList->AddText(ImVec2(textX, textY), ImGui::GetColorU32(ImGuiCol_TextDisabled), index.c_str());
						const float nameX = textX + ImGui::CalcTextSize("#00").x + style.ItemSpacing.x * 0.5f;
						drawList->AddText(ImVec2(std::max(nameX, textX + ImGui::CalcTextSize(index.c_str()).x + style.ItemSpacing.x * 0.5f), textY),
							ImGui::GetColorU32(ignored ? ImGuiCol_TextDisabled : ImGuiCol_Text), name.c_str());
						drawList->PopClipRect();
					}

					// 光标回到行底边再提交一个空 item：ImGui 不允许只用 SetCursorPos 撑大窗口
					ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y));
					ImGui::Dummy(ImVec2(0.0f, 0.0f));
					if (selected && shapePanel.scrolledTo != shapePanel.selected)
					{
						ImGui::SetScrollHereY(0.5f);
						shapePanel.scrolledTo = shapePanel.selected;
					}
					ImGui::PopID();
				}
			}
			clipper.End();
			ImGui::PopStyleVar();
		}
		ImGui::EndChild();
		if (children.size() > 1)
			ImGui::TextDisabled("单击选中 · 右键复制 / 移动 / 删除 · 悬停在视口里高亮");
	}

	// 视口高亮：悬停的行优先，否则选中的形状（只在显示了这个实体的碰撞体时看得到）
	if (hoveredRow >= 0)
		PhysicsDebugView::SetHoveredShape(entity, hoveredRow);
	else if (shapePanel.selected >= 0 && shapePanel.selected < static_cast<int>(children.size())
		&& PhysicsDebugView::GetMode() != PhysicsDebugView::Mode::Off)
		PhysicsDebugView::SetHoveredShape(entity, shapePanel.selected);

	// ======================== 选中形状的参数 ========================
	if (shapePanel.selected >= 0 && shapePanel.selected < static_cast<int>(children.size()) && children[shapePanel.selected])
	{
		const size_t i = static_cast<size_t>(shapePanel.selected);
		const ChildShape& shape = *children[i];
		const bool characterShape = physics.isCharacter && i == 0;		// 角色控制器忽略它的偏移和旋转
		ImGui::PushID(static_cast<int>(i));

		// ---- 头部：#序号 类型                      [复制] [↑] [↓] [删除] ----
		ImGui::AlignTextToFramePadding();
		ImGui::Text("#%d %s", static_cast<int>(i), shapeName(shape.shapeType));
		if (physics.isCharacter && i > 0)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("（角色控制器忽略）");
		}
		{
			const char* labels[] = { "复制", "上移", "下移", "删除" };
			float buttonsWidth = 0.0f;
			for (const char* label : labels)
				buttonsWidth += ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f;
			buttonsWidth += style.ItemInnerSpacing.x * 3.0f;
			ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - buttonsWidth));
			const auto button = [&](const char* label, bool enabled, ShapeAction a)
				{
					if (!enabled)
						ImGui::BeginDisabled();
					if (ImGui::Button(label))
					{
						action = a;
						actionIndex = i;
					}
					if (!enabled)
						ImGui::EndDisabled();
				};
			button("复制", true, ShapeAction::Duplicate);
			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
			button("上移", i > 0, ShapeAction::MoveUp);
			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
			button("下移", i + 1 < children.size(), ShapeAction::MoveDown);
			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
			button("删除", true, ShapeAction::Remove);
		}

		// 先在局部副本上改；改了才复制一份子形状（写时复制，三角网格的顶点也只在改的时候复制一次）
		ShapeType type = shape.shapeType;
		glm::vec3 size = shape.size;
		glm::vec3 position = shape.position;
		glm::quat rotation = shape.rotation;
		bool shapeChanged = false;
		bool rotationChanged = false;

		if (beginTable("##ShapeTable"))
		{
			row("Type");
			if (isPrimitive(type))
			{
				int typeIndex = static_cast<int>(type);
				const char* primitives[] = { "Box", "Sphere", "Capsule", "Cylinder" };
				if (ImGui::Combo("##Type", &typeIndex, primitives, IM_ARRAYSIZE(primitives)) && typeIndex != static_cast<int>(type))
				{
					const ShapeType newType = static_cast<ShapeType>(typeIndex);
					size = convertSize(type, newType, size);
					type = newType;
					shapeChanged = true;
				}
			}
			else
			{
				ImGui::AlignTextToFramePadding();
				ImGui::TextDisabled("%s（%s，由模型生成，这里不能改）", shapeName(type), sizeSummary(shape).c_str());
			}

			const char* scaleNote = "尺寸会再乘上 Transform 的缩放";
			switch (type)
			{
			case ShapeType::Box:
			{
				glm::vec3 full = size * 2.0f;
				row("Size", scaleNote);
				if (ImGui::DragFloat3("##Size", glm::value_ptr(full), 0.01f, 0.002f, 1.0e4f, "%.3f", kClamp))
				{
					size = full * 0.5f;
					shapeChanged = true;
				}
				break;
			}
			case ShapeType::Sphere:
			{
				float radius = size.x;
				row("Radius", scaleNote);
				if (ImGui::DragFloat("##Radius", &radius, 0.01f, 0.001f, 1.0e4f, "%.3f", kClamp))
				{
					size = glm::vec3(radius);
					shapeChanged = true;
				}
				break;
			}
			case ShapeType::Capsule:
			{
				float radius = size.x;
				float height = size.y;
				row("Radius", scaleNote);
				if (ImGui::DragFloat("##Radius", &radius, 0.01f, 0.001f, 1.0e4f, "%.3f", kClamp))
					shapeChanged = true;
				row("Height", "两端半球之间圆柱部分的高度（总高 = 高度 + 2 × 半径）");
				if (ImGui::DragFloat("##Height", &height, 0.01f, 0.0f, 1.0e4f, "%.3f", kClamp))
					shapeChanged = true;
				size = glm::vec3(radius, height, radius);
				break;
			}
			case ShapeType::Cylinder:
			{
				float radius = size.x;
				float height = size.y * 2.0f;
				row("Radius", scaleNote);
				if (ImGui::DragFloat("##Radius", &radius, 0.01f, 0.001f, 1.0e4f, "%.3f", kClamp))
					shapeChanged = true;
				row("Height", "总高度");
				if (ImGui::DragFloat("##Height", &height, 0.01f, 0.002f, 1.0e4f, "%.3f", kClamp))
					shapeChanged = true;
				size = glm::vec3(radius, height * 0.5f, radius);
				break;
			}
			default:
				break;
			}

			// 基本形状：一键包住模型（尺寸、偏移按模型包围盒，旋转清零）
			if (isPrimitive(type) && hasModel)
			{
				row("Fit", "按模型的包围盒设置尺寸和偏移，旋转清零");
				if (ImGui::Button("包住模型", ImVec2(-FLT_MIN, 0.0f)))
				{
					if (auto fitted = fitToModel(type))
					{
						size = fitted->size;
						if (!characterShape)
							position = fitted->position;
						rotation = glm::identity<glm::quat>();
						shapeChanged = true;
						rotationChanged = true;
					}
				}
			}

			const char* characterReason = "角色控制器忽略第一个形状的偏移和旋转";
			row("Offset", "相对实体中心的偏移");
			shapeChanged |= disabledIf(characterShape, characterReason,
				[&] { return ImGui::DragFloat3("##Offset", glm::value_ptr(position), 0.01f); });
			row("Rotation", "相对实体的旋转（度）");
			rotationChanged |= disabledIf(characterShape, characterReason,
				[&] { return EditorRotation::DragEuler("##Rotation", rotation); });
			ImGui::EndTable();
		}

		if (shapeChanged || rotationChanged)
		{
			auto copy = std::make_shared<ChildShape>(shape);
			copy->shapeType = type;
			copy->size = size;
			copy->position = position;
			if (rotationChanged)
				copy->rotation = rotation;
			children[i] = std::move(copy);
			changed = true;
		}
		ImGui::PopID();
	}

	// 列表画完再改结构，免得循环里下标错位；选中跟着形状走
	if (action != ShapeAction::None && actionIndex < children.size())
	{
		const auto at = children.begin() + static_cast<std::ptrdiff_t>(actionIndex);
		int newSelected = static_cast<int>(actionIndex);
		switch (action)
		{
		case ShapeAction::Duplicate:
			if (children[actionIndex])
				children.insert(at + 1, std::make_shared<ChildShape>(*children[actionIndex]));
			newSelected = static_cast<int>(actionIndex) + 1;
			break;
		case ShapeAction::MoveUp:
			if (actionIndex > 0)
			{
				std::swap(children[actionIndex], children[actionIndex - 1]);
				newSelected = static_cast<int>(actionIndex) - 1;
			}
			break;
		case ShapeAction::MoveDown:
			if (actionIndex + 1 < children.size())
			{
				std::swap(children[actionIndex], children[actionIndex + 1]);
				newSelected = static_cast<int>(actionIndex) + 1;
			}
			break;
		case ShapeAction::MoveFirst:
			std::rotate(children.begin(), at, at + 1);
			newSelected = 0;
			break;
		case ShapeAction::Remove:
			children.erase(at);
			newSelected = children.empty() ? -1 : std::min(static_cast<int>(actionIndex), static_cast<int>(children.size()) - 1);
			break;
		default:
			break;
		}
		shapePanel.selected = newSelected;
		changed = true;
	}

	return changed;
}

// ---------------- 手写面板共用：标签 | 控件 [| 复位] 表格 ----------------
namespace PanelRows
{
	inline const ImVec4 kWarnColor(1.0f, 0.75f, 0.3f, 1.0f);

	inline float LabelWidth(std::initializer_list<const char*> labels)
	{
		float width = 0.0f;
		for (const char* label : labels)
			width = std::max(width, ImGui::CalcTextSize(label).x);
		return width + ImGui::GetStyle().ItemSpacing.x * 2.0f;
	}

	inline float ResetColumnWidth()
	{
		return ImGui::CalcTextSize("复位").x + ImGui::GetStyle().FramePadding.x * 2.0f;
	}

	inline bool Begin(const char* id, float labelWidth, bool resetColumn = false)
	{
		if (!ImGui::BeginTable(id, resetColumn ? 3 : 2, ImGuiTableFlags_SizingFixedFit))
			return false;
		ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
		ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
		if (resetColumn)
			ImGui::TableSetupColumn("reset", ImGuiTableColumnFlags_WidthFixed, ResetColumnWidth());
		return true;
	}

	// 一行：左标签（可带悬停说明，可变灰），进入控件列，控件宽度占满
	inline void Row(const char* label, const char* tooltip = nullptr, bool disabled = false)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		if (disabled)
			ImGui::TextDisabled("%s", label);
		else
			ImGui::TextUnformatted(label);
		if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("%s", tooltip);
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-FLT_MIN);
	}

	// 第三列的复位按钮：已经是默认值时变灰（不隐藏，保持对齐）
	inline bool ResetCell(const char* id, bool isDefault, const char* tooltip)
	{
		ImGui::TableSetColumnIndex(2);
		ImGui::PushID(id);
		ImGui::BeginDisabled(isDefault);
		const bool clicked = ImGui::Button("复位");
		ImGui::EndDisabled();
		if (!isDefault && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", tooltip);
		ImGui::PopID();
		return clicked;
	}

	inline void Warn(const char* text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, kWarnColor);
		ImGui::TextWrapped("%s", text);
		ImGui::PopStyleColor();
	}

	inline void Note(const char* text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextWrapped("%s", text);
		ImGui::PopStyleColor();
	}
}

// ---------------- Transform ----------------
// 位置 / 旋转 / 缩放三行，每行右边一个复位；缩放可以锁定等比。
// 复位 = 组件默认值：位置 (0,0,0)、旋转 (0,0,0)（不旋转）、缩放 (1,1,1)
template<>
inline bool PropertiesHelper::DrawComponent<Transform>(Transform& trans)
{
	bool changed = false;
	static bool s_uniformScale = false;		// 等比缩放：所有实体共用一个开关
	const ImGuiStyle& style = ImGui::GetStyle();

	if (PanelRows::Begin("##Transform", PanelRows::LabelWidth({ "Position", "Rotation", "Scale" }), true))
	{
		// ---- 位置 ----
		PanelRows::Row("Position", "世界坐标（米）。引擎目前没有父子层级，这里就是世界位置");
		changed |= ImGui::DragFloat3("##Position", glm::value_ptr(trans.position), 0.05f, 0.0f, 0.0f, "%.3f");
		if (PanelRows::ResetCell("##ResetPosition", trans.position == glm::vec3(0.0f), "复位到原点 (0, 0, 0)"))
		{
			trans.position = glm::vec3(0.0f);
			changed = true;
		}

		// ---- 旋转 ----
		PanelRows::Row("Rotation",
			"欧拉角（度）：依次绕世界 X、Y、Z 轴旋转（先 X，再 Y，最后 Z）。\n"
			"可以超过 ±180 / ±360，显示的就是你输入的写法；Gizmo 等别处改了旋转时，\n"
			"会换算成和当前显示最接近的写法，数值不会跳。\n"
			"Y = ±90° 时 X 和 Z 绕的是同一根轴（万向节锁），改哪个效果一样");
		changed |= EditorRotation::DragEuler("##Rotation", trans.rotation, 0.5f, "%.1f°");
		{
			const glm::vec3 shown = EditorRotation::DisplayedEuler("##Rotation");
			const bool isDefault = EditorRotation::SameRotation(trans.rotation, glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
				&& glm::all(glm::lessThan(glm::abs(shown), glm::vec3(1.0e-4f)));
			if (PanelRows::ResetCell("##ResetRotation", isDefault, "复位到不旋转 (0, 0, 0)"))
			{
				EditorRotation::SetEuler("##Rotation", trans.rotation, glm::vec3(0.0f));
				changed = true;
			}
		}

		// ---- 缩放 ----
		PanelRows::Row("Scale", "各轴缩放倍数（1 = 原始大小）");
		{
			const bool locked = s_uniformScale;
			if (locked)
				ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
			if (ImGui::Button("等比##UniformScale"))
				s_uniformScale = !s_uniformScale;
			if (locked)
				ImGui::PopStyleColor();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(locked ? "等比缩放：开（改一个分量，另外两个按同样比例变）\n点一下关闭" : "等比缩放：关\n点一下打开");
			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
			ImGui::SetNextItemWidth(-FLT_MIN);

			glm::vec3 scale = trans.scale;
			if (ImGui::DragFloat3("##Scale", glm::value_ptr(scale), 0.01f, 0.0f, 0.0f, "%.3f"))
			{
				if (s_uniformScale)
				{
					// 找出改的那个分量，按它的比例改另外两个；原来是 0 没法算比例，就三个都设成新值
					for (int axis = 0; axis < 3; ++axis)
					{
						if (scale[axis] == trans.scale[axis])
							continue;
						const float before = trans.scale[axis];
						const float after = scale[axis];
						if (std::abs(before) > 1.0e-6f)
							scale = trans.scale * (after / before);
						else
							scale = glm::vec3(after);
						scale[axis] = after;
						break;
					}
				}
				trans.scale = scale;
				changed = true;
			}
		}
		if (PanelRows::ResetCell("##ResetScale", trans.scale == glm::vec3(1.0f), "复位到原始大小 (1, 1, 1)"))
		{
			trans.scale = glm::vec3(1.0f);
			changed = true;
		}

		ImGui::EndTable();
	}

	if (trans.scale.x == 0.0f || trans.scale.y == 0.0f || trans.scale.z == 0.0f)
		PanelRows::Warn("缩放有分量为 0：物体被压扁成面 / 线，法线和物理形状会出问题");
	else if (trans.scale.x < 0.0f || trans.scale.y < 0.0f || trans.scale.z < 0.0f)
		PanelRows::Warn("负缩放会翻转三角形绕序，开了背面剔除的材质可能显示成反面");

	return changed;
}

// ---------------- 相机 ----------------
template<>
inline bool PropertiesHelper::DrawComponent<CameraComponent>(CameraComponent& camera)
{
	bool changed = false;
	constexpr ImGuiSliderFlags kClamp = ImGuiSliderFlags_AlwaysClamp;
	const CameraComponent defaults;
	Entity entity = ImguiLayout::selectedEntity;
	const bool hasFollow = entity && entity.hasComponent<CameraFollow>();

	if (PanelRows::Begin("##Camera", PanelRows::LabelWidth({ "Field of View", "Near Plane", "Far Plane", "World Up", "First Person" }), true))
	{
		PanelRows::Row("Field of View", "垂直视场角（度）。越大看到的越多，透视变形也越明显；一般 60~90");
		changed |= ImGui::DragFloat("##FOV", &camera.fov, 0.2f, 1.0f, 179.0f, "%.1f°", kClamp);
		if (PanelRows::ResetCell("##ResetFOV", camera.fov == defaults.fov, "复位到 75°"))
		{
			camera.fov = defaults.fov;
			changed = true;
		}

		// 近 / 远平面互相限制：近 < 远
		const float nearMax = std::max(0.001f, camera.farPlane * 0.999f);
		PanelRows::Row("Near Plane", "近裁剪面（米）：比它更近的东西不画。\n太小会降低深度精度，远处容易闪烁（Z-fighting），够用就别调太小");
		changed |= ImGui::DragFloat("##Near", &camera.nearPlane, 0.005f, 0.001f, nearMax, "%.3f m", kClamp);
		if (PanelRows::ResetCell("##ResetNear", camera.nearPlane == defaults.nearPlane, "复位到 0.1 m"))
		{
			camera.nearPlane = std::min(defaults.nearPlane, nearMax);
			changed = true;
		}

		const float farMin = camera.nearPlane * 1.001f + 0.001f;
		PanelRows::Row("Far Plane", "远裁剪面（米）：比它更远的东西不画。\n方向光阴影也只算到 min(远平面, 灯光的阴影距离)");
		changed |= ImGui::DragFloat("##Far", &camera.farPlane, 1.0f, farMin, 1.0e6f, "%.1f m", kClamp);
		if (PanelRows::ResetCell("##ResetFar", camera.farPlane == defaults.farPlane, "复位到 200 m"))
		{
			camera.farPlane = std::max(defaults.farPlane, farMin);
			changed = true;
		}

		PanelRows::Row("World Up", "相机的“上”方向，第一人称跟随时用来算朝向。一般就是 (0, 1, 0)；松手后自动归一化");
		changed |= ImGui::DragFloat3("##WorldUp", glm::value_ptr(camera.worldUp), 0.01f, -1.0f, 1.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			const float length = glm::length(camera.worldUp);
			if (length > 1.0e-4f && std::abs(length - 1.0f) > 1.0e-4f)
			{
				camera.worldUp /= length;
				changed = true;
			}
		}
		if (PanelRows::ResetCell("##ResetUp", camera.worldUp == defaults.worldUp, "复位到 (0, 1, 0)"))
		{
			camera.worldUp = defaults.worldUp;
			changed = true;
		}

		PanelRows::Row("First Person",
			"第一人称跟随（配合 Camera Follow 组件）：\n"
			"跟随目标有 Controller 时，相机放在 目标位置 + 目标旋转 × 偏移，\n"
			"朝向跟着 Controller 的 yaw / pitch，并把目标水平转到相机朝向。\n"
			"关掉时 Camera Follow 目前什么都不做（还没有第三人称实现）",
			!hasFollow);
		changed |= ImGui::Checkbox("##FirstPerson", &camera.isFirstPerson);
		ImGui::EndTable();
	}

	if (glm::length(camera.worldUp) < 1.0e-4f)
		PanelRows::Warn("World Up 是零向量，算不出相机朝向");
	if (!hasFollow)
		PanelRows::Note("这个实体没有 Camera Follow 组件，First Person 不起作用");

	return changed;
}

// ---------------- 移动 ----------------
// 只有 Can Jump 是配置；移动方向 / 想跳是输入（PlayerInput、脚本）每帧写的运行时状态，只显示（写回时也不覆盖，见 EditorSnapshot<Movement>）
template<>
inline bool PropertiesHelper::DrawComponent<Movement>(Movement& movement)
{
	bool changed = false;
	if (PanelRows::Begin("##Movement", PanelRows::LabelWidth({ "Can Jump", "Move Direction", "Want Jump" })))
	{
		PanelRows::Row("Can Jump", "关掉后跳跃请求会被忽略");
		changed |= ImGui::Checkbox("##CanJump", &movement.canJump);

		ImGui::BeginDisabled();
		PanelRows::Row("Move Direction", "当前移动方向（世界空间，已归一化），运行时由输入写入");
		ImGui::AlignTextToFramePadding();
		ImGui::Text("(%.2f, %.2f, %.2f)", movement.currentMoveDirection.x, movement.currentMoveDirection.y, movement.currentMoveDirection.z);
		PanelRows::Row("Want Jump", "这一帧是否请求跳跃，运行时由输入写入");
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(movement.currentWantJump ? "是" : "否");
		ImGui::EndDisabled();
		ImGui::EndTable();
	}
	PanelRows::Note("灰色两项是运行时状态，只显示，不能在这里改");
	return changed;
}

// ---------------- 灯光 ----------------
namespace LightPanelDetail
{
	inline BaseLightData& Base(RenderLight& light)
	{
		return std::visit([](auto& data) -> BaseLightData& { return data; }, light.data);
	}

	// 换类型时保留共有的设置（颜色、阴影、体积光），点光 / 聚光之间还保留强度
	inline void ChangeType(RenderLight& light, LightType type)
	{
		const BaseLightData base = Base(light);
		std::optional<float> candela;
		if (const auto* point = light.GetData<PointLightData>()) candela = point->cdIntensity;
		if (const auto* spot = light.GetData<SpotLightData>()) candela = spot->cdIntensity;

		switch (type)
		{
		case LightType::Directional:
		{
			DirectionalLightData data;
			static_cast<BaseLightData&>(data) = base;
			light.data = data;
			break;
		}
		case LightType::Point:
		{
			PointLightData data;
			static_cast<BaseLightData&>(data) = base;
			if (candela) data.cdIntensity = *candela;
			light.data = data;
			break;
		}
		case LightType::Spot:
		{
			SpotLightData data;
			static_cast<BaseLightData&>(data) = base;
			if (candela) data.cdIntensity = *candela;
			light.data = data;
			break;
		}
		}
		light.type = type;
	}

	inline constexpr uint32_t kShadowMapSizes[] = { 256, 512, 1024, 2048, 4096 };
}

template<>
inline bool PropertiesHelper::DrawComponent<RenderLight>(RenderLight& light)
{
	using namespace LightPanelDetail;
	bool changed = false;
	constexpr ImGuiSliderFlags kClamp = ImGuiSliderFlags_AlwaysClamp;

	// type 和 data 对不上（旧数据）：按 type 换成对应的数据
	if (light.data.index() != static_cast<size_t>(light.type))
	{
		ChangeType(light, light.type);
		changed = true;
	}

	auto* directional = light.GetData<DirectionalLightData>();
	auto* point = light.GetData<PointLightData>();
	auto* spot = light.GetData<SpotLightData>();
	BaseLightData& base = Base(light);
	const float labelWidth = PanelRows::LabelWidth({ "Shadow Map Size", "Cascade Split", "Affected By Sky" });

	// ======================== 基本 ========================
	ImGui::SeparatorText("Light");
	if (PanelRows::Begin("##LightBasic", labelWidth))
	{
		PanelRows::Row("Type", "Directional：平行光（太阳），只看方向，位置无关\nPoint：点光，向四周发光\nSpot：聚光，锥形\n换类型会保留颜色、阴影、体积光设置");
		int typeIndex = static_cast<int>(light.type);
		const char* typeNames[] = { "Directional（平行光）", "Point（点光）", "Spot（聚光）" };
		if (ImGui::Combo("##Type", &typeIndex, typeNames, IM_ARRAYSIZE(typeNames)) && typeIndex != static_cast<int>(light.type))
		{
			ChangeType(light, static_cast<LightType>(typeIndex));
			ImGui::EndTable();
			return true;		// data 换了，这一帧不再画旧数据的引用
		}

		PanelRows::Row("Visible", "关掉后这盏灯不参与渲染");
		changed |= ImGui::Checkbox("##Visible", &light.visible);
		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 3.0f);
		changed |= ImGui::Checkbox("显示光源形状", &light.renderCube);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("在场景里画一个小方块标出灯的位置（调试用）");

		PanelRows::Row("Color", "灯光颜色（线性）");
		changed |= ImGui::ColorEdit3("##Color", glm::value_ptr(base.color), ImGuiColorEditFlags_Float);

		if (directional)
		{
			PanelRows::Row("Intensity", "照度（lux），乘在颜色上");
			changed |= ImGui::DragFloat("##Intensity", &directional->luxIntensity, 0.05f, 0.0f, 200000.0f, "%.2f lux", kClamp);
		}
		else
		{
			float& candela = point ? point->cdIntensity : spot->cdIntensity;
			PanelRows::Row("Intensity", "发光强度（坎德拉 cd），乘在颜色上；照度随距离平方衰减");
			changed |= ImGui::DragFloat("##Intensity", &candela, 1.0f, 0.0f, 1.0e7f, "%.1f cd", kClamp);
		}

		// 方向 / 位置来自 Transform：这里只显示
		if (auto* transform = ImguiLayout::selectedEntity ? ImguiLayout::selectedEntity.tryGetComponent<Transform>() : nullptr)
		{
			if (directional || spot)
			{
				const glm::vec3 direction = transform->GetDirection();
				PanelRows::Row("Direction", "照射方向 = 物体 Transform 旋转后的 +Z 方向，用旋转或 Gizmo 调。\n高度角：光从上往下照为正（太阳在地平线以上）");
				ImGui::AlignTextToFramePadding();
				ImGui::TextDisabled("(%.2f, %.2f, %.2f)  高度角 %.1f°", direction.x, direction.y, direction.z,
					glm::degrees(std::asin(std::clamp(-direction.y, -1.0f, 1.0f))));
			}
			if (!directional)
			{
				PanelRows::Row("Position", "灯的位置 = 物体 Transform 位置");
				ImGui::AlignTextToFramePadding();
				ImGui::TextDisabled("(%.2f, %.2f, %.2f)", transform->position.x, transform->position.y, transform->position.z);
			}
		}
		ImGui::EndTable();
	}

	// ======================== 聚光锥 ========================
	if (spot)
	{
		ImGui::SeparatorText("Spot Cone");
		if (PanelRows::Begin("##LightSpot", labelWidth))
		{
			PanelRows::Row("Inner Angle", "内锥半角（度）：这个角度以内是全亮");
			if (ImGui::DragFloat("##Inner", &spot->cutOffAngle, 0.2f, 0.0f, 89.0f, "%.1f°", kClamp))
			{
				spot->outercutOffAngle = std::max(spot->outercutOffAngle, spot->cutOffAngle);
				changed = true;
			}
			PanelRows::Row("Outer Angle", "外锥半角（度）：内角到外角之间逐渐变暗，外角以外不亮。不会小于内角");
			if (ImGui::DragFloat("##Outer", &spot->outercutOffAngle, 0.2f, 0.0f, 89.0f, "%.1f°", kClamp))
			{
				spot->cutOffAngle = std::min(spot->cutOffAngle, spot->outercutOffAngle);
				changed = true;
			}
			ImGui::EndTable();
		}
	}

	// ======================== 阴影 ========================
	ImGui::SeparatorText("Shadow");
	if (PanelRows::Begin("##LightShadow", labelWidth))
	{
		PanelRows::Row("Cast Shadow");
		changed |= ImGui::Checkbox("##CastShadow", &base.castShadow);

		const bool noShadow = !base.castShadow;
		ImGui::BeginDisabled(noShadow);

		PanelRows::Row("Shadow Map Size",
			directional ? "每级级联的阴影图边长（像素）：第一级用这个尺寸，后面逐级变小（不小于一半）。越大越清晰，越占显存"
			: point ? "阴影图边长（像素）。点光要画 6 个面，占用是聚光的 6 倍"
			: "阴影图边长（像素）。越大越清晰，越占显存",
			noShadow);
		{
			char preview[32];
			std::snprintf(preview, sizeof(preview), "%u × %u", base.shadowMapSize, base.shadowMapSize);
			if (ImGui::BeginCombo("##ShadowMapSize", preview))
			{
				for (uint32_t size : kShadowMapSizes)
				{
					char item[32];
					std::snprintf(item, sizeof(item), "%u × %u", size, size);
					if (ImGui::Selectable(item, size == base.shadowMapSize))
					{
						base.shadowMapSize = size;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
		}

		if (directional)
		{
			PanelRows::Row("Shadow Distance", "阴影距离（米）：只在相机前这么远内算阴影，末尾 10% 渐隐。\n实际还会被相机远平面限制", noShadow);
			changed |= ImGui::DragFloat("##ShadowDistance", &directional->shadowDistance, 1.0f, 1.0f, 100000.0f, "%.0f m", kClamp);

			PanelRows::Row("Cascades", "级联层数：近处用小范围高精度的阴影图，远处用大范围的。层数越多越清晰也越贵", noShadow);
			int cascades = static_cast<int>(directional->cascadeLevel);
			if (ImGui::SliderInt("##Cascades", &cascades, 1, 8, "%d", kClamp))
			{
				directional->cascadeLevel = static_cast<uint32_t>(cascades);
				changed = true;
			}

			PanelRows::Row("Cascade Split", "级联切分方式：0 = 均匀切分，1 = 对数切分（近处更密）。一般 0.6~0.9", noShadow);
			changed |= ImGui::SliderFloat("##CascadeSplit", &directional->cascadeSplitLambda, 0.0f, 1.0f, "%.2f", kClamp);
		}

		PanelRows::Row("Normal Bias", "法线偏移（阴影图像素）：沿法线把接收点往外推，主要消除斜面上的条纹（shadow acne）。\n太大阴影会和物体脱开（漏光）", noShadow);
		changed |= ImGui::DragFloat("##NormalBias", &base.shadowNormalBias, 0.01f, 0.0f, 10.0f, "%.2f", kClamp);

		PanelRows::Row("Slope Bias", "斜率深度偏移：画阴影图时按三角形斜率把深度推远。\n至少 PCF 半径 + 0.5（3×3 PCF 时 ≥ 1.5）才不会出痘；太大会漏光", noShadow);
		changed |= ImGui::DragFloat("##SlopeBias", &base.shadowSlopeBias, 0.01f, 0.0f, 10.0f, "%.2f", kClamp);

		ImGui::EndDisabled();
		ImGui::EndTable();
	}

	// ======================== 体积光 ========================
	ImGui::SeparatorText("Volumetric");
	if (PanelRows::Begin("##LightVolume", labelWidth))
	{
		PanelRows::Row("Enable", "这盏灯是否照亮体积雾（光柱效果）");
		changed |= ImGui::Checkbox("##VolumeEnable", &base.volumeLightEnable);

		const bool noVolume = !base.volumeLightEnable;
		ImGui::BeginDisabled(noVolume);
		PanelRows::Row("Intensity", "体积散射强度倍数（只影响体积雾里的光，不影响表面）", noVolume);
		changed |= ImGui::DragFloat("##VolumeIntensity", &base.volumetricScatteringIntensity, 0.01f, 0.0f, 100.0f, "%.2f", kClamp);
		PanelRows::Row("Affected By Sky", "光穿过大气时按大气透过率衰减（日落时光柱变红变暗）。\n需要场景开了大气体积", noVolume);
		changed |= ImGui::Checkbox("##AffectedBySky", &base.volumeLightEffectByAtmospher);
		ImGui::EndDisabled();
		ImGui::EndTable();
	}

	return changed;
}

template<typename ModelComponent>
bool PropertiesHelper::DrawModelComponent(ModelComponent& comp, ProjectManager* assetManager, const char* modelLabel)
{
	if (!comp.model)
	{
		ImGui::TextDisabled("No Model assigned");
		return false;
	}

	auto database = assetManager ? assetManager->GetDatabase() : nullptr;
	const ImGuiStyle& style = ImGui::GetStyle();
	const float frameHeight = ImGui::GetFrameHeight();
	const ImVec4 errorColor(1.0f, 0.35f, 0.35f, 1.0f);
	const char* const alphaModes[] = { "Opaque", "Mask", "Blend" };
	const auto alphaModeName = [&alphaModes](AlphaMode mode)
		{
			return alphaModes[std::min<uint32_t>(static_cast<uint32_t>(mode), 2u)];
		};
	bool anyChanged = false;

	// label | value 两列表格的一行：进入数值列，控件宽度占满
	const auto beginInfoRow = [](const char* label)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-FLT_MIN);
		};

	// ======================== 模型 ========================
	{
		const AssetGUID& modelGuid = comp.model->GetAssetGUID();
		auto modelMeta = (database && !modelGuid.empty()) ? database->GetAssetMetaByGuid(modelGuid) : nullptr;
		const float labelWidth = std::max(ImGui::CalcTextSize(modelLabel).x, ImGui::CalcTextSize("Visible").x) + style.ItemSpacing.x;
		if (ImGui::BeginTable("##ModelInfo", 2, ImGuiTableFlags_SizingFixedFit))
		{
			ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
			ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

			beginInfoRow(modelLabel);
			ImGui::AlignTextToFramePadding();
			if (modelGuid.empty())
				ImGui::TextDisabled("Unlinked（不是工程资产里的模型）");
			else if (!modelMeta)
				ImGui::TextColored(errorColor, "Missing asset (%s)", modelGuid.c_str());
			else
			{
				ImGui::TextUnformatted(modelMeta->name.c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("%s", modelMeta->path.c_str());
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", modelMeta->path.c_str());
			}

			beginInfoRow("Visible");
			anyChanged |= ImGui::Checkbox("##Visible", &comp.visible);
			ImGui::EndTable();
		}
	}

	struct MaterialEdit
	{
		size_t slot;
		MaterialProperties properties;
		std::shared_ptr<Material> material;
		AssetGUID guid;
	};
	struct MaterialDrop
	{
		size_t slot;
		std::shared_ptr<Material> material;
	};
	std::vector<MaterialEdit> edits;
	std::vector<MaterialDrop> drops;
	auto& meshInfos = comp.model->GetMeshInfos();

	// ---- 松手才提交：拖动 / 输入期间改动先暂存，没有控件处于激活状态时才变成一次 edit ----
	//   资产材质：提交 = 写回材质资产并存盘；实例材质：提交 = 克隆一份实例模型 / 材质。拖动期间每帧都提交的话，
	//   每帧都要存一次盘（资产浏览器缩略图跟着每帧重画）或克隆一次模型。
	//   暂存期间：能安全直接改的材质（资产材质，或者这个实体已经有自己的实例材质——有 materialOverrideProperties 的槽，
	//   加载场景和上次提交时都为它单独建了 Material）实时 SetProperty，场景和材质球马上看到效果；
	//   还和模型资产共用的实例材质不能直接改（会改到其他实体），只在面板里显示暂存值，松手克隆后才生效。
	//   暂存按"这个组件面板 + 槽号"记，同时记下材质对象：槽被换了材质就作废
	struct PendingMaterialEdit
	{
		const Material* material = nullptr;
		MaterialProperties properties;
	};
	static std::unordered_map<uint64_t, PendingMaterialEdit> s_pendingMaterialEdits;

	const ImGuiID panelId = ImGui::GetID("##materialSlots");
	const auto pendingKey = [panelId](size_t slot) { return (static_cast<uint64_t>(panelId) << 32) | static_cast<uint32_t>(slot); };
	const auto canEditLive = [&](size_t slot)
		{
			const auto& material = meshInfos[slot].material;
			return material && (!material->GetAssetGUID().empty()
				|| comp.materialOverrideProperties.contains(static_cast<uint32_t>(slot)));
		};
	const auto effectiveProperties = [&](size_t slot) -> MaterialProperties
		{
			const auto& material = meshInfos[slot].material;
			auto it = s_pendingMaterialEdits.find(pendingKey(slot));
			if (it != s_pendingMaterialEdits.end())
			{
				if (it->second.material == material.get())
					return it->second.properties;
				s_pendingMaterialEdits.erase(it);
			}
			return material ? material->GetProperties() : MaterialProperties{};
		};
	const auto stageEdit = [&](size_t slot, const MaterialProperties& properties)
		{
			const auto& material = meshInfos[slot].material;
			if (!material)
				return;
			s_pendingMaterialEdits[pendingKey(slot)] = { material.get(), properties };
			if (canEditLive(slot))
				material->SetProperty(properties);
		};
	// 这个面板暂存的改动：没有控件在拖动 / 输入时统一提交（只遍历暂存表里属于这个面板的项，不扫全部槽）
	const auto commitReleased = [&]()
		{
			if (ImGui::IsAnyItemActive())
				return;
			for (auto it = s_pendingMaterialEdits.begin(); it != s_pendingMaterialEdits.end();)
			{
				if ((it->first >> 32) != panelId)
				{
					++it;
					continue;
				}
				const size_t slot = static_cast<size_t>(it->first & 0xFFFFFFFFull);
				if (slot < meshInfos.size())
				{
					const auto& material = meshInfos[slot].material;
					if (material && it->second.material == material.get())
						edits.push_back({ slot, it->second.properties, material, material->GetAssetGUID() });
				}
				it = s_pendingMaterialEdits.erase(it);
			}
		};

	const auto materialDisplayName = [&](size_t slot) -> std::string
		{
			const auto& material = meshInfos[slot].material;
			if (!material)
				return "No material";
			const AssetGUID& guid = material->GetAssetGUID();
			if (guid.empty())
				return "无资产材质";
			auto meta = database ? database->GetAssetMetaByGuid(guid) : nullptr;
			return meta ? meta->name : std::string("资产不存在");
		};

	ImGui::SeparatorText(std::format("Materials ({})", meshInfos.size()).c_str());
	if (meshInfos.empty())
		ImGui::TextDisabled("这个模型没有材质槽");

	// 面板状态（按面板 ID 存）：选中的槽、搜索词、列表高度
	struct SlotPanelState
	{
		int selected = -1;
		char filter[128] = {};
		std::vector<size_t> filtered;		// 搜索结果（槽号）
		std::string filterKey;				// 算 filtered 时的搜索词
		const void* filterModel = nullptr;	// 算 filtered 时的模型
		size_t filterCount = 0;
		int filterFrame = -1000;
	};
	static std::unordered_map<ImGuiID, SlotPanelState> s_slotPanels;
	SlotPanelState& panel = s_slotPanels[panelId];
	if (panel.selected >= static_cast<int>(meshInfos.size()))
		panel.selected = -1;
	if (panel.selected < 0 && meshInfos.size() == 1)
		panel.selected = 0;

	// ======================== 批量设置（多个槽时才有，默认收起；展开时才去读各槽属性） ========================
	if (meshInfos.size() > 1 && ImGui::TreeNodeEx("##BatchEdit", ImGuiTreeNodeFlags_SpanAvailWidth, "批量设置（所有 %zu 个槽）", meshInfos.size()))
	{
		float sharedOpacity = 0.f;
		int sharedAlphaMode = 0;
		bool hasShared = false;
		bool mixedOpacity = false;
		bool mixedAlphaMode = false;
		for (size_t slot = 0; slot < meshInfos.size(); ++slot)
		{
			if (!meshInfos[slot].material)
				continue;
			const MaterialProperties properties = effectiveProperties(slot);
			if (!hasShared)
			{
				sharedOpacity = properties.opacity;
				sharedAlphaMode = static_cast<int>(std::min<uint32_t>(static_cast<uint32_t>(properties.alphamode), 2u));
				hasShared = true;
			}
			else
			{
				if (properties.opacity - sharedOpacity > 1e-4f || sharedOpacity - properties.opacity > 1e-4f)
					mixedOpacity = true;
				if (static_cast<int>(properties.alphamode) != sharedAlphaMode)
					mixedAlphaMode = true;
			}
		}

		if (!hasShared)
			ImGui::TextDisabled("所有槽都没有材质");
		else
		{
			// 改动暂存到每个槽，松手后统一提交
			const auto applyAll = [&](const auto& modify)
				{
					for (size_t slot = 0; slot < meshInfos.size(); ++slot)
					{
						if (!meshInfos[slot].material)
							continue;
						MaterialProperties properties = effectiveProperties(slot);
						modify(properties);
						stageEdit(slot, properties);
					}
				};

			const float labelWidth = ImGui::CalcTextSize("Alpha Mode").x + style.ItemSpacing.x;
			if (ImGui::BeginTable("##BatchTable", 2, ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
				ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

				beginInfoRow("Opacity");
				if (ImGui::SliderFloat("##AllOpacity", &sharedOpacity, 0.0f, 1.0f, mixedOpacity ? "%.3f（各槽不同）" : "%.3f"))
					applyAll([&](MaterialProperties& p) { p.opacity = sharedOpacity; });

				beginInfoRow("Alpha Mode");
				if (ImGui::BeginCombo("##AllAlphaMode", mixedAlphaMode ? "各槽不同" : alphaModes[sharedAlphaMode]))
				{
					for (int i = 0; i < IM_ARRAYSIZE(alphaModes); ++i)
					{
						if (ImGui::Selectable(alphaModes[i], !mixedAlphaMode && i == sharedAlphaMode))
							applyAll([i](MaterialProperties& p) { p.alphamode = static_cast<AlphaMode>(static_cast<uint32_t>(i)); });
					}
					ImGui::EndCombo();
				}
				ImGui::EndTable();
			}
		}
		ImGui::TreePop();
	}

	// ======================== 槽列表：搜索 + 固定高度、可拖动调高的列表（只画看得见的行） ========================
	// 行：[小材质球] #槽号 材质名                     Blend · 双面
	//   单击选中（预览和参数在列表下面），双击打开材质编辑器，把材质资产拖到行上替换这个槽；悬停材质球看大图
	const float kRowPad = 2.0f;
	const float kRowThumb = std::floor(frameHeight + 2.0f);		// 一行文字高的小材质球
	const float rowHeight = kRowThumb + kRowPad * 2.0f;
	constexpr float kRowSpacing = 1.0f;							// 行间距（列表里单独设，比默认紧）
	const float rowStride = rowHeight + kRowSpacing;

	if (!meshInfos.empty())
	{
		// ---- 搜索：按槽号或材质名（空格分隔多个词，全部包含才算），结果缓存，词 / 模型变了或每 30 帧重算一次 ----
		if (meshInfos.size() > 1)
		{
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputTextWithHint("##SlotFilter", "搜索槽号或材质名（空格分隔多个词）", panel.filter, sizeof(panel.filter), ImGuiInputTextFlags_EscapeClearsAll);
		}
		const std::string filterText = panel.filter;
		const int frame = ImGui::GetFrameCount();
		if (filterText != panel.filterKey || panel.filterModel != comp.model.get() || panel.filterCount != meshInfos.size()
			|| (!filterText.empty() && frame - panel.filterFrame >= 30))
		{
			panel.filterKey = filterText;
			panel.filterModel = comp.model.get();
			panel.filterCount = meshInfos.size();
			panel.filterFrame = frame;
			panel.filtered.clear();

			std::vector<std::string> terms;
			{
				std::string term;
				for (char c : filterText)
				{
					if (c == ' ' || c == '\t')
					{
						if (!term.empty())
							terms.push_back(Tool::ToLower(term));
						term.clear();
					}
					else
						term += c;
				}
				if (!term.empty())
					terms.push_back(Tool::ToLower(term));
			}

			panel.filtered.reserve(meshInfos.size());
			for (size_t slot = 0; slot < meshInfos.size(); ++slot)
			{
				if (terms.empty())
				{
					panel.filtered.push_back(slot);
					continue;
				}
				const std::string haystack = Tool::ToLower(std::format("#{} {}", slot, materialDisplayName(slot)));
				bool match = true;
				for (const auto& term : terms)
				{
					if (haystack.find(term) == std::string::npos)
					{
						match = false;
						break;
					}
				}
				if (match)
					panel.filtered.push_back(slot);
			}
		}

		if (!filterText.empty())
			ImGui::TextDisabled("找到 %zu / %zu 个槽", panel.filtered.size(), meshInfos.size());

		// ---- 列表：默认最多显示 8 行高，底边可以拖动调高（ImGui 会记住） ----
		const size_t visibleRows = std::clamp<size_t>(panel.filtered.size(), 1, 8);
		const float listHeight = visibleRows * rowStride - kRowSpacing + style.WindowPadding.y * 2.0f;
		ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, rowStride + style.WindowPadding.y * 2.0f), ImVec2(FLT_MAX, FLT_MAX));
		if (ImGui::BeginChild("##SlotList", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, kRowSpacing));
			if (panel.filtered.empty())
				ImGui::TextDisabled("没有匹配的槽");

			// 选中项变了（例如在场景里点选）时滚到它
			static std::unordered_map<ImGuiID, int> s_scrolledTo;
			int& scrolledTo = s_scrolledTo[panelId];

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(panel.filtered.size()), rowStride);
			if (panel.selected >= 0 && scrolledTo != panel.selected)
			{
				auto it = std::find(panel.filtered.begin(), panel.filtered.end(), static_cast<size_t>(panel.selected));
				if (it != panel.filtered.end())
					clipper.IncludeItemByIndex(static_cast<int>(it - panel.filtered.begin()));
			}
			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const size_t slot = panel.filtered[row];
					ImGui::PushID(static_cast<int>(slot));
					auto& material = meshInfos[slot].material;
					const AssetGUID materialGuid = material ? material->GetAssetGUID() : AssetGUID{};
					auto materialMeta = (database && !materialGuid.empty()) ? database->GetAssetMetaByGuid(materialGuid) : nullptr;
					const bool isInstance = material && materialGuid.empty();
					const bool missingAsset = !materialGuid.empty() && !materialMeta;
					const bool canOpenEditor = materialMeta && materialMeta->type == AssetType::Material;
					const std::string materialName = !material ? std::string("No material")
						: materialMeta ? materialMeta->name
						: isInstance ? std::string("无资产材质")
						: std::string("资产不存在");
					const bool selected = panel.selected == static_cast<int>(slot);

					const auto acceptMaterialDrop = [&]()
						{
							if (!ImGui::BeginDragDropTarget())
								return;
							if (const auto* payload = ImGui::AcceptDragDropPayload("ASSET_GUID"))
							{
								const auto* guidData = static_cast<const char*>(payload->Data);
								if (guidData && guidData[0] != '\0' && database && assetManager)
								{
									auto droppedMeta = database->GetAssetMetaByGuid(AssetGUID(guidData));
									if (droppedMeta && droppedMeta->type == AssetType::Material)
									{
										auto asset = assetManager->LoadAsset(droppedMeta->guid);
										auto replacement = asset ? asset->AsMaterial() : nullptr;
										if (replacement)
											drops.push_back({ slot, std::move(replacement) });
									}
								}
							}
							ImGui::EndDragDropTarget();
						};

					const ImVec2 p0 = ImGui::GetCursorScreenPos();
					const float rowWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
					const ImVec2 p1(p0.x + rowWidth, p0.y + rowHeight);

					// 整行是一个按钮（点击、拖放、悬停）；上面的材质球允许叠在它上面
					ImGui::SetNextItemAllowOverlap();
					ImGui::InvisibleButton("##row", ImVec2(rowWidth, rowHeight));
					const bool rowHovered = ImGui::IsItemHovered();
					if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
					{
						panel.selected = static_cast<int>(slot);
						scrolledTo = panel.selected;		// 自己点的不用再滚
					}
					if (rowHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && canOpenEditor)
						ImguiLayout::OpenMaterialEditor(materialMeta->guid);
					acceptMaterialDrop();

					// 只有选中 / 悬停时才画底色，像列表项而不是一张张卡片
					ImDrawList* drawList = ImGui::GetWindowDrawList();
					if (selected || rowHovered)
						drawList->AddRectFilled(p0, p1, ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered), style.FrameRounding);

					// 小材质球（悬停显示大图）：只有画到的行才会请求渲染
					ImGui::SetCursorScreenPos(ImVec2(p0.x + kRowPad, p0.y + kRowPad));
					if (AssetThumbnails::DrawMaterialBox(assetManager, material, kRowThumb, materialName.c_str()) && canOpenEditor)
						ImguiLayout::OpenMaterialEditor(materialMeta->guid);
					acceptMaterialDrop();

					// 一行文字：#槽号 材质名，右边靠右是透明模式 / 双面（材质名太长时被裁掉，不压到右边的标签）
					{
						const float textY = p0.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
						const float textX = p0.x + kRowPad + kRowThumb + style.ItemSpacing.x;
						std::string tag;
						if (material)
						{
							const MaterialProperties properties = effectiveProperties(slot);
							if (properties.alphamode != AlphaMode::Opaque)
								tag = alphaModeName(properties.alphamode);
							if (properties.twosided)
								tag += tag.empty() ? "双面" : " · 双面";
						}
						const float tagWidth = tag.empty() ? 0.0f : ImGui::CalcTextSize(tag.c_str()).x;
						const float tagX = p1.x - style.FramePadding.x - tagWidth;
						if (!tag.empty())
							drawList->AddText(ImVec2(tagX, textY), ImGui::GetColorU32(ImGuiCol_TextDisabled), tag.c_str());

						const float nameRight = tag.empty() ? p1.x - style.FramePadding.x : tagX - style.ItemSpacing.x;
						const std::string slotTag = std::format("#{}", slot);
						drawList->PushClipRect(ImVec2(textX, p0.y), ImVec2(std::max(textX, nameRight), p1.y), true);
						drawList->AddText(ImVec2(textX, textY), ImGui::GetColorU32(ImGuiCol_TextDisabled), slotTag.c_str());
						const float nameX = textX + ImGui::CalcTextSize(slotTag.c_str()).x + style.ItemSpacing.x * 0.5f;
						const ImU32 nameColor = missingAsset ? ImGui::ColorConvertFloat4ToU32(errorColor)
							: ImGui::GetColorU32(material && !isInstance ? ImGuiCol_Text : ImGuiCol_TextDisabled);
						drawList->AddText(ImVec2(nameX, textY), nameColor, materialName.c_str());
						drawList->PopClipRect();
					}

					// 光标回到行底边再提交一个空 item：ImGui 不允许只用 SetCursorPos 撑大窗口。
					// Dummy 之后光标在 p1.y + kRowSpacing，正好是 clipper 的行高 rowStride
					ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y));
					ImGui::Dummy(ImVec2(0.0f, 0.0f));

					if (selected && scrolledTo != panel.selected)
					{
						ImGui::SetScrollHereY(0.5f);
						scrolledTo = panel.selected;
					}
					ImGui::PopID();
				}
			}
			clipper.End();
			ImGui::PopStyleVar();
		}
		ImGui::EndChild();
		if (meshInfos.size() > 1)
			ImGui::TextDisabled("单击选中 · 双击打开材质编辑器 · 材质资产拖到行上或下面的头部替换");
	}

	// ======================== 选中槽的参数（和材质编辑器同一套排法：[贴图] 名称 [数值]） ========================
	if (panel.selected >= 0)
	{
		const size_t slot = static_cast<size_t>(panel.selected);
		auto& material = meshInfos[slot].material;
		const AssetGUID materialGuid = material ? material->GetAssetGUID() : AssetGUID{};
		auto materialMeta = (database && !materialGuid.empty()) ? database->GetAssetMetaByGuid(materialGuid) : nullptr;
		const bool isInstance = material && materialGuid.empty();
		const bool canOpenEditor = materialMeta && materialMeta->type == AssetType::Material;
		const float textureSide = std::floor(frameHeight * 1.25f);
		const float propLabelWidth = ImGui::CalcTextSize("Emission Strength").x + style.ItemSpacing.x;

		ImGui::PushID(static_cast<int>(slot));
		ImGui::Separator();

		// ---- 头部：[材质球] 名称 / 路径 / 槽号 · 透明模式 · 双面 · 贴图数            [编辑] ----
		{
			constexpr float kHeaderThumb = 64.0f;
			const ImVec2 h0 = ImGui::GetCursorScreenPos();
			const float headerWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);

			if (AssetThumbnails::DrawMaterialBox(assetManager, material, kHeaderThumb, materialDisplayName(slot).c_str()) && canOpenEditor)
				ImguiLayout::OpenMaterialEditor(materialMeta->guid);

			const char* editLabel = "编辑";
			const float editWidth = ImGui::CalcTextSize(editLabel).x + style.FramePadding.x * 2.0f;
			const float infoX = h0.x + kHeaderThumb + style.ItemSpacing.x;
			const float infoRight = h0.x + headerWidth - editWidth - style.ItemSpacing.x;

			// 三行信息，裁剪到编辑按钮左边
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
			const float infoTop = h0.y + (kHeaderThumb - lineHeight * 3.0f + style.ItemSpacing.y) * 0.5f;
			std::string line2;
			std::string line3 = std::format("#{}", slot);
			if (!material)
				line2 = "没有材质：把材质资产拖到这里";
			else
			{
				const MaterialProperties properties = effectiveProperties(slot);
				line2 = materialMeta ? materialMeta->path
					: isInstance ? std::string(canEditLive(slot) ? "无资产材质 · 属性覆盖存在场景里，只影响这个实体"
						: "无资产材质 · 和模型共用，松手后为这个实体生成属性覆盖")
					: std::format("资产不存在：{}", materialGuid);
				line3 += std::format(" · {}", alphaModeName(properties.alphamode));
				if (properties.twosided)
					line3 += " · 双面";
				size_t textureCount = 0;
				for (const auto& [type, texture] : material->GetTextures())
					textureCount += (texture && !texture->IsEmpty()) ? 1 : 0;
				line3 += textureCount ? std::format(" · {} 张贴图", textureCount) : std::string(" · 没有贴图");
			}
			const bool missingAsset = !materialGuid.empty() && !materialMeta;
			drawList->PushClipRect(ImVec2(infoX, h0.y), ImVec2(std::max(infoX, infoRight), h0.y + kHeaderThumb), true);
			drawList->AddText(ImVec2(infoX, infoTop), missingAsset ? ImGui::ColorConvertFloat4ToU32(errorColor) : ImGui::GetColorU32(ImGuiCol_Text),
				materialDisplayName(slot).c_str());
			drawList->AddText(ImVec2(infoX, infoTop + lineHeight), ImGui::GetColorU32(ImGuiCol_TextDisabled), line2.c_str());
			drawList->AddText(ImVec2(infoX, infoTop + lineHeight * 2.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), line3.c_str());
			drawList->PopClipRect();

			// 信息区域可以悬停看完整路径（窄面板上会被裁掉）
			ImGui::SetCursorScreenPos(ImVec2(infoX, h0.y));
			ImGui::InvisibleButton("##info", ImVec2(std::max(1.0f, infoRight - infoX), kHeaderThumb));
			if (ImGui::IsItemHovered())
			{
				if (canOpenEditor)
					ImGui::SetTooltip("%s\n%s\n修改松手时保存到材质资产，所有用到它的地方都会变；换贴图请到材质编辑器", materialDisplayName(slot).c_str(), line2.c_str());
				else
					ImGui::SetTooltip("%s\n%s", materialDisplayName(slot).c_str(), line2.c_str());
			}

			// 编辑按钮：右边，垂直居中
			ImGui::SetCursorScreenPos(ImVec2(h0.x + headerWidth - editWidth, h0.y + (kHeaderThumb - frameHeight) * 0.5f));
			ImGui::BeginDisabled(!canOpenEditor);
			if (ImGui::Button(editLabel, ImVec2(editWidth, 0.0f)))
				ImguiLayout::OpenMaterialEditor(materialMeta->guid);
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("%s", canOpenEditor ? "在材质编辑器中打开"
					: isInstance ? "这个槽的材质没有资产，不能在材质编辑器里打开（在下面直接改，作为属性覆盖存进场景）"
					: "没有可以打开的材质资产");

			// 整个头部（材质球 + 信息 + 按钮）是一个拖放目标：把材质资产拖上来直接替换这个槽。
			// 只有拖的是别的材质资产时才接（才画高亮框），拖贴图等其他资产经过时不响应
			{
				const ImRect headerRect(h0, ImVec2(h0.x + headerWidth, h0.y + kHeaderThumb));
				if (ImGui::BeginDragDropTargetCustom(headerRect, ImGui::GetID("##headerDrop")))
				{
					const ImGuiPayload* peek = ImGui::GetDragDropPayload();
					std::shared_ptr<AssetMeta> droppedMeta;
					if (peek && peek->IsDataType("ASSET_GUID") && peek->Data && database)
					{
						const auto* guidData = static_cast<const char*>(peek->Data);
						if (guidData[0] != '\0')
							droppedMeta = database->GetAssetMetaByGuid(AssetGUID(guidData));
					}
					if (droppedMeta && droppedMeta->type == AssetType::Material && droppedMeta->guid != materialGuid)
					{
						if (ImGui::AcceptDragDropPayload("ASSET_GUID") && assetManager)
						{
							auto asset = assetManager->LoadAsset(droppedMeta->guid);
							auto replacement = asset ? asset->AsMaterial() : nullptr;
							if (replacement)
								drops.push_back({ slot, std::move(replacement) });
						}
						else
							ImGui::SetTooltip("替换 #%zu 的材质为：%s", slot, droppedMeta->name.c_str());
					}
					ImGui::EndDragDropTarget();
				}
			}

			// 光标放到头部下面，再提交一个空 item（不能只靠 SetCursorPos 撑大窗口）
			ImGui::SetCursorScreenPos(ImVec2(h0.x, h0.y + kHeaderThumb));
			ImGui::Dummy(ImVec2(0.0f, 0.0f));
		}

		if (material)
		{
			auto properties = effectiveProperties(slot);
			const auto textures = material->GetTextures();
			bool slotChanged = false;

			const auto textureOf = [&textures](TextureType type) -> std::shared_ptr<Texture2D>
				{
					auto it = textures.find(type);
					return (it != textures.end() && it->second && !it->second->IsEmpty()) ? it->second : nullptr;
				};
			const auto hasTexture = [&](TextureType type) { return textureOf(type) != nullptr; };
			const auto textureDisplayName = [&](const std::shared_ptr<Texture2D>& texture) -> std::string
				{
					if (!texture)
						return "没有贴图";
					const AssetGUID& guid = texture->GetAssetGUID();
					if (guid.empty())
						return std::format("无资产贴图（{}×{}）", texture->GetWidth(), texture->GetHeight());
					auto meta = database ? database->GetAssetMetaByGuid(guid) : nullptr;
					return meta ? meta->name : std::format("资产不存在：{}", guid);
				};

			// 贴图格只读：资产贴图用缩略图（后台生成、带悬停大图），没有资产的贴图直接画已经加载好的那张
			const auto drawTextureSlot = [&](TextureType type)
				{
					auto texture = textureOf(type);
					if (texture && !texture->GetAssetGUID().empty())
					{
						AssetThumbnails::DrawThumbnailBox(assetManager, texture->GetAssetGUID(), textureSide);
						return;
					}
					const ImVec2 q0 = ImGui::GetCursorScreenPos();
					const ImVec2 q1(q0.x + textureSide, q0.y + textureSide);
					ImGui::Dummy(ImVec2(textureSide, textureSide));
					if (!ImGui::IsItemVisible())
						return;
					ImDrawList* dl = ImGui::GetWindowDrawList();
					dl->AddRectFilled(q0, q1, IM_COL32(40, 40, 40, 255), 3.0f);
					if (texture)
						Sprite2DEditor::DrawFrameInRect(dl, Render2D::Sprite::MakeFrame(texture, nullptr), ImVec2(q0.x + 2.0f, q0.y + 2.0f), ImVec2(q1.x - 2.0f, q1.y - 2.0f));
					dl->AddRect(q0, q1, IM_COL32(95, 95, 95, 255), 3.0f);
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", texture ? textureDisplayName(texture).c_str() : "没有贴图（贴图在材质编辑器里设置）");
				};

			const auto beginSection = [&](const char* title) -> bool
				{
					ImGui::SeparatorText(title);
					if (!ImGui::BeginTable(title, 3, ImGuiTableFlags_SizingFixedFit))
						return false;
					ImGui::TableSetupColumn("tex", ImGuiTableColumnFlags_WidthFixed, textureSide);
					ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, propLabelWidth);
					ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
					return true;
				};
			const auto beginRow = [&](const char* label, std::optional<TextureType> textureType)
				{
					const float offsetY = (textureSide - frameHeight) * 0.5f;
					ImGui::TableNextRow(ImGuiTableRowFlags_None, textureSide);
					ImGui::TableSetColumnIndex(0);
					if (textureType)
						drawTextureSlot(*textureType);
					ImGui::TableSetColumnIndex(1);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offsetY);
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(label);
					ImGui::TableSetColumnIndex(2);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offsetY);
					ImGui::SetNextItemWidth(-FLT_MIN);
				};
			const auto overridden = [](bool disabled, const char* reason, auto&& draw) -> bool
				{
					if (disabled)
						ImGui::BeginDisabled();
					const bool edited = draw();
					if (disabled)
					{
						ImGui::EndDisabled();
						if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
							ImGui::SetTooltip("%s", reason);
					}
					return edited;
				};
			const auto textureNameCell = [&](TextureType type)
				{
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(textureDisplayName(textureOf(type)).c_str());
				};

			const bool metallicRoughnessTex = hasTexture(TextureType::MetallicRoughness);

			if (beginSection("Surface"))
			{
				beginRow("Albedo", TextureType::Albedo);
				slotChanged |= ImGui::ColorEdit3("##Albedo", glm::value_ptr(properties.albedo));
				if (hasTexture(TextureType::Albedo) && ImGui::IsItemHovered())
					ImGui::SetTooltip("和 Albedo 贴图相乘");

				beginRow("Metallic", TextureType::Metallic);
				slotChanged |= overridden(metallicRoughnessTex || hasTexture(TextureType::Metallic), "由贴图提供（Metallic / Metallic Roughness 贴图的 B 通道），数值不生效",
					[&] { return ImGui::DragFloat("##Metallic", &properties.metallic, 0.01f, 0.0f, 1.0f); });

				beginRow("Roughness", TextureType::Roughness);
				slotChanged |= overridden(metallicRoughnessTex || hasTexture(TextureType::Roughness), "由贴图提供（Roughness / Metallic Roughness 贴图的 G 通道），数值不生效",
					[&] { return ImGui::DragFloat("##Roughness", &properties.roughness, 0.01f, 0.0f, 1.0f); });

				if (metallicRoughnessTex)
				{
					beginRow("Metallic Roughness", TextureType::MetallicRoughness);
					textureNameCell(TextureType::MetallicRoughness);
				}
				if (hasTexture(TextureType::Normal))
				{
					beginRow("Normal", TextureType::Normal);
					textureNameCell(TextureType::Normal);
				}

				beginRow("Ambient Occlusion", TextureType::AO);
				slotChanged |= overridden(hasTexture(TextureType::AO), "由 AO 贴图（R 通道）提供，数值不生效",
					[&] { return ImGui::DragFloat("##AO", &properties.ambientOcclusion, 0.01f, 0.0f, 1.0f); });

				if (hasTexture(TextureType::Height))
				{
					beginRow("Height", TextureType::Height);
					textureNameCell(TextureType::Height);
				}
				ImGui::EndTable();
			}

			if (beginSection("Emission"))
			{
				beginRow("Emission Color", TextureType::Emissive);
				slotChanged |= overridden(hasTexture(TextureType::Emissive), "由自发光贴图提供，颜色不生效（强度仍然生效）",
					[&] { return ImGui::ColorEdit3("##EmissionColor", glm::value_ptr(properties.emissionColor)); });

				beginRow("Emission Strength", std::nullopt);
				slotChanged |= ImGui::DragFloat("##EmissionStrength", &properties.emissionStrength, 0.01f, 0.0f, 100.0f);
				ImGui::EndTable();
			}

			if (beginSection("Transparency"))
			{
				int alphaMode = static_cast<int>(std::min<uint32_t>(static_cast<uint32_t>(properties.alphamode), 2u));
				beginRow("Alpha Mode", std::nullopt);
				if (ImGui::Combo("##AlphaMode", &alphaMode, alphaModes, IM_ARRAYSIZE(alphaModes)))
				{
					properties.alphamode = static_cast<AlphaMode>(static_cast<uint32_t>(alphaMode));
					slotChanged = true;
				}

				beginRow("Opacity", TextureType::Opacity);
				slotChanged |= ImGui::DragFloat("##Opacity", &properties.opacity, 0.01f, 0.0f, 1.0f);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("和 Opacity 贴图（R 通道）相乘；没有 Opacity 贴图时取 Albedo 贴图的 alpha");

				if (properties.alphamode == AlphaMode::Mask)
				{
					beginRow("Mask Threshold", std::nullopt);
					slotChanged |= ImGui::DragFloat("##MaskThreshold", &properties.maskthreshold, 0.01f, 0.0f, 1.0f);
				}

				beginRow("IOR", std::nullopt);
				slotChanged |= ImGui::DragFloat("##IOR", &properties.IOR, 0.01f, 1.0f, 3.0f);
				ImGui::EndTable();
			}

			if (beginSection("Rendering"))
			{
				beginRow("Two Sided", std::nullopt);
				slotChanged |= ImGui::Checkbox("##TwoSided", &properties.twosided);
				ImGui::EndTable();
			}

			if (slotChanged)
				stageEdit(slot, properties);
		}
		ImGui::PopID();
	}
	else if (meshInfos.size() > 1)
	{
		ImGui::TextDisabled("在上面的列表里选一个槽查看和修改参数");
	}

	// 批量设置和选中槽的改动：松手后统一提交
	commitReleased();

	decltype(comp.model) instanceModel;
	for (const auto& edit : edits)
	{
		if (!edit.guid.empty())
		{
			if (edit.material)
				edit.material->SetProperty(edit.properties);
			if (assetManager)
			{
				auto asset = assetManager->LoadAsset(edit.guid);
				auto materialAsset = asset ? std::dynamic_pointer_cast<MaterialAsset>(asset) : nullptr;
				if (materialAsset)
				{
					materialAsset->SetProperties(edit.properties);
					// 存盘后资产浏览器里的材质球按新的元文件重画
					if (assetManager->SaveAsset(edit.guid))
						AssetThumbnails::Invalidate(edit.guid);
				}
			}
			continue;
		}
		else
		{
			if (!instanceModel)
				instanceModel = comp.model->Clone(false, false);
			auto& editedMeshInfos = instanceModel->GetMeshInfos();
			if (edit.slot >= editedMeshInfos.size())
				continue;
			auto materialInstance = editedMeshInfos[edit.slot].material
				? editedMeshInfos[edit.slot].material->Clone()
				: std::make_shared<Material>();
			materialInstance->SetProperty(edit.properties);
			editedMeshInfos[edit.slot].material = std::move(materialInstance);
			const uint32_t index = static_cast<uint32_t>(edit.slot);
			comp.materialOverrideGUIDs.erase(index);
			comp.materialOverrideProperties[index] = edit.properties;
		}
	}

	if (!drops.empty())
	{
		if (!instanceModel)
			instanceModel = comp.model->Clone(false, false);
		auto& editedMeshInfos = instanceModel->GetMeshInfos();
		for (const auto& drop : drops)
		{
			if (drop.slot >= editedMeshInfos.size())
				continue;
			editedMeshInfos[drop.slot].material = drop.material;
			const uint32_t index = static_cast<uint32_t>(drop.slot);
			const auto& guid = drop.material->GetAssetGUID();
			if (guid.empty())
			{
				comp.materialOverrideGUIDs.erase(index);
				comp.materialOverrideProperties[index] = drop.material->GetProperties();
			}
			else
			{
				comp.materialOverrideGUIDs[index] = guid;
				comp.materialOverrideProperties.erase(index);
			}
		}
	}
	if (instanceModel)
		comp.model = std::move(instanceModel);

	return anyChanged || !edits.empty() || !drops.empty();
}

template<>
inline bool PropertiesHelper::DrawComponent<RenderStaticModel>(RenderStaticModel& comp, ProjectManager* assetManager)
{
	return DrawModelComponent(comp, assetManager, "StaticModel");
}

template<>
inline bool PropertiesHelper::DrawComponent<RenderSkinnedModel>(RenderSkinnedModel& comp, ProjectManager* assetManager)
{
	return DrawModelComponent(comp, assetManager, "SkinnedModel");
}

template<>
inline bool PropertiesHelper::DrawComponent<SkeletonAnimatorGroup>(SkeletonAnimatorGroup& comp, ProjectManager* assetManager)
{
	bool anyChanged = false;
	auto database = assetManager ? assetManager->GetDatabase() : nullptr;

	const auto assetLabel = [database](const AssetGUID& guid, const char* emptyLabel)
		{
			if (guid.empty())
				return std::string(emptyLabel);
			auto meta = database ? database->GetAssetMetaByGuid(guid) : nullptr;
			return meta ? meta->name : guid;
		};
	const auto acceptDrop = [database](AssetType type, AssetGUID& target)
		{
			if (!ImGui::BeginDragDropTarget())
				return false;
			bool accepted = false;
			if (const auto* payload = ImGui::AcceptDragDropPayload("ASSET_GUID"))
			{
				const auto* guidData = static_cast<const char*>(payload->Data);
				auto dropped = (guidData && guidData[0] != 0 && database) ? database->GetAssetMetaByGuid(AssetGUID(guidData)) : nullptr;
				if (dropped && dropped->type == type)
				{
					target = dropped->guid;
					accepted = true;
				}
			}
			ImGui::EndDragDropTarget();
			return accepted;
		};

	// 骨架是独立资产，这里只存 GUID；拖一个 Skeleton 进来。
	ImGui::TextUnformatted("Skeleton");
	ImGui::SameLine();
	ImGui::Button(assetLabel(comp.skeletonGuid, "No skeleton assigned").c_str(), ImVec2(220.f, 0.f));
	if (acceptDrop(AssetType::Skeleton, comp.skeletonGuid))
	{
		// 换了骨架，所有 animator 都得按新骨架重建，palette 长度才对得上。
		auto asset = assetManager ? assetManager->LoadAsset(comp.skeletonGuid) : nullptr;
		comp.SetSkeleton(asset ? asset->AsSkeleton() : nullptr, comp.skeletonGuid);
		anyChanged = true;
	}

	// 名字是 map 的键，编辑期间只改缓冲区，回车或失焦后才落到容器里，避免遍历时改 map。
	static std::unordered_map<std::string, std::string> nameEdits;
	std::string removeName;
	std::string renameFrom;
	std::string renameTo;
	std::string clipDropName;

	for (auto& [name, data] : comp.animatorDatas)
	{
		if (!data)
			continue;

		ImGui::PushID(name.c_str());

		ImGui::BeginChild("##animator_block", ImVec2(0.f, 104.f), ImGuiChildFlags_Borders, ImGuiWindowFlags_None);

		bool enabled = data->enable;
		if (ImGui::Checkbox("##enable", &enabled))
		{
			data->enable = enabled;
			anyChanged = true;
		}

		ImGui::SameLine();
		auto editIt = nameEdits.find(name);
		if (editIt == nameEdits.end())
			editIt = nameEdits.emplace(name, name).first;

		char nameBuffer[64] = {};
		std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", editIt->second.c_str());

		// 空名字或重名先标红，回车或失焦时才真正改。
		const bool nameInvalid = editIt->second.empty() ||
			(editIt->second != name && comp.ExistAnimator(editIt->second));
		if (nameInvalid)
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.45f, 0.15f, 0.15f, 1.f));

		ImGui::SetNextItemWidth(160.f);
		const bool nameSubmitted =
			ImGui::InputText("##name", nameBuffer, sizeof(nameBuffer),
				ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll) ||
			ImGui::IsItemDeactivatedAfterEdit();

		if (nameInvalid)
			ImGui::PopStyleColor();

		editIt->second = nameBuffer;
		if (nameSubmitted)
		{
			renameFrom = name;
			renameTo = editIt->second;
		}

		ImGui::SameLine();
		if (ImGui::SmallButton("Remove"))
			removeName = name;

		// Clip：拖动画资产进来，GUID 先记下，资产在遍历之后统一加载。
		ImGui::TextUnformatted("Clip");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1.f);
		ImGui::Button(assetLabel(data->clipGuid, "No clip assigned").c_str(), ImVec2(-1.f, 0.f));
		if (acceptDrop(AssetType::AnimationClip, data->clipGuid))
			clipDropName = name;

		// Loop + 播放进度：进度直接问 animator，不要另存一份时间。
		ImGui::TextUnformatted("Loop");
		ImGui::SameLine();
		int loopCount = data->maxLoopCount;
		ImGui::SetNextItemWidth(90.f);
		if (ImGui::DragInt("##loop", &loopCount, 0.1f, 0, 99))
		{
			data->maxLoopCount = loopCount;
			anyChanged = true;
		}

		ImGui::SameLine();
		if (data->animator)
		{
			const float duration = data->animator->GetDurationTime();
			const float current = data->animator->GetCurrentTime();
			const float progress = duration > 0.f ? std::clamp(current / duration, 0.f, 1.f) : 0.f;
			char overlay[64] = {};
			std::snprintf(overlay, sizeof(overlay), "%.2f / %.2fs", current, duration);
			ImGui::ProgressBar(progress, ImVec2(-1.f, 0.f), overlay);
		}
		else
			ImGui::TextDisabled("no animator yet (need skeleton + clip)");

		ImGui::EndChild();
		ImGui::PopID();
	}

	// 拖进来的 clip：按 GUID 去工程里取资产，取不到就当没拖。
	if (!clipDropName.empty())
	{
		auto it = comp.animatorDatas.find(clipDropName);
		if (it != comp.animatorDatas.end() && it->second)
		{
			auto asset = assetManager ? assetManager->LoadAsset(it->second->clipGuid) : nullptr;
			comp.SetClip(clipDropName, asset ? asset->AsAnimationClip() : nullptr, it->second->clipGuid);
			anyChanged = true;
		}
	}

	if (!renameFrom.empty() && renameTo != renameFrom)
	{
		if (comp.RenameAnimator(renameFrom, renameTo))
		{
			nameEdits.erase(renameFrom);
			anyChanged = true;
		}
		else
		{
			nameEdits[renameFrom] = renameFrom;
			ImGui::TextColored(ImVec4(1.f, 0.45f, 0.45f, 1.f), "Name must be non-empty and unique.");
		}
	}

	if (ImGui::Button("Add Animation"))
	{
		for (int index = 1; ; ++index)
		{
			std::string candidate = std::format("animator_{}", index);
			if (comp.ExistAnimator(candidate))
				continue;
			comp.AddAnimatorSlot(candidate, AssetGUID());
			anyChanged = true;
			break;
		}
	}

	if (!removeName.empty())
	{
		comp.RemoveAnimator(removeName);
		anyChanged = true;
	}

	ImGui::TextUnformatted("Default");
	ImGui::SameLine();
	const char* preview = comp.defaultAnimator.empty() ? "(none)" : comp.defaultAnimator.c_str();
	if (ImGui::BeginCombo("##default_animator", preview))
	{
		if (ImGui::Selectable("(none)", comp.defaultAnimator.empty()))
		{
			comp.defaultAnimator.clear();
			anyChanged = true;
		}
		for (auto& [name, data] : comp.animatorDatas)
		{
			if (ImGui::Selectable(name.c_str(), comp.defaultAnimator == name))
			{
				comp.defaultAnimator = name;
				anyChanged = true;
			}
		}
		ImGui::EndCombo();
	}

	ImGui::TextUnformatted("Idle");
	ImGui::SameLine();
	const char* idlePreview = comp.idleAnimator.empty() ? "(none)" : comp.idleAnimator.c_str();
	if (ImGui::BeginCombo("##idle_animator", idlePreview))
	{
		if (ImGui::Selectable("(none)", comp.idleAnimator.empty()))
		{
			comp.idleAnimator.clear();
			anyChanged = true;
		}
		for (auto& [name, data] : comp.animatorDatas)
		{
			if (ImGui::Selectable(name.c_str(), comp.idleAnimator == name))
			{
				comp.idleAnimator = name;
				anyChanged = true;
			}
		}
		ImGui::EndCombo();
	}

	return anyChanged;
}

// 粒子发射器：生成器/更新器是多态槽位，只能手写抽屉——先选种类，再画这个种类的字段。
template<>
inline bool PropertiesHelper::DrawComponent<ParticleEmitter>(ParticleEmitter& comp, ProjectManager* assetManager)
{
	bool changed = false;
	auto& p = comp.emitterParams;
	auto database = assetManager ? assetManager->GetDatabase() : nullptr;

	const auto assetLabel = [database](const AssetGUID& guid, const char* emptyLabel)
		{
			if (guid.empty())
				return std::string(emptyLabel);
			auto meta = database ? database->GetAssetMetaByGuid(guid) : nullptr;
			return meta ? meta->name : guid;
		};
	const auto acceptDrop = [database](AssetType type, AssetGUID& target)
		{
			if (!ImGui::BeginDragDropTarget())
				return false;
			bool accepted = false;
			if (const auto* payload = ImGui::AcceptDragDropPayload("ASSET_GUID"))
			{
				const auto* guidData = static_cast<const char*>(payload->Data);
				auto dropped = (guidData && guidData[0] != 0 && database) ? database->GetAssetMetaByGuid(AssetGUID(guidData)) : nullptr;
				if (dropped && dropped->type == type)
				{
					target = dropped->guid;
					accepted = true;
				}
			}
			ImGui::EndDragDropTarget();
			return accepted;
		};
	// 返回 -1 = 没选；否则是选中的种类下标。
	const auto kindCombo = [](const char* id, const char* const* names, int count, int current)
		{
			int picked = -1;
			ImGui::SetNextItemWidth(150.f);
			if (ImGui::BeginCombo(id, current >= 0 ? names[current] : "(unset)"))
			{
				for (int index = 0; index < count; ++index)
					if (ImGui::Selectable(names[index], index == current))
						picked = index;
				ImGui::EndCombo();
			}
			return picked;
		};

	if (ImGui::Checkbox("Enable", &comp.enable))
		changed = true;

	ImGui::TextDisabled("Emission");
	ImGui::SetNextItemWidth(150.f);
	changed |= ImGui::DragFloat("Emit Rate", &p.emitRate, 0.1f, 0.f, 10000.f);
	ImGui::SetNextItemWidth(150.f);
	changed |= ImGui::DragFloat("Rate Jitter", &p.emitRateJitter, 0.005f, 0.f, 1.f);
	ImGui::SetNextItemWidth(150.f);
	changed |= ImGui::DragInt("Max Particles", &p.maxParticles, 10.0f, 0, 1000000);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("这个发射器同时存活的粒子上限（默认 10000）。\n达到上限后本帧多出来的发射直接丢弃，不会补发");
	p.maxParticles = std::max(p.maxParticles, 0);

	ImGui::SetNextItemWidth(150.f);
	changed |= ImGui::DragInt("Burst Amount", &p.burstAmount, 0.2f, 0, 100000);
	ImGui::SetNextItemWidth(150.f);
	changed |= ImGui::DragInt("Burst Count", &p.burstCount, 0.2f, 0, 100000);
	ImGui::SetNextItemWidth(150.f);
	changed |= ImGui::DragFloat("Burst Interval", &p.burstInterval, 0.01f, 0.f, 1000.f);
	ImGui::SetNextItemWidth(150.f);
	changed |= ImGui::DragFloat("Warmup Time", &p.warmupTime, 0.01f, 0.f, 1000.f);

	ImGui::TextDisabled("Appearance");
	{
		static const char* rendererNames[] = { "Billboard", "Mesh" };
		const int current = std::holds_alternative<ParticleMeshRenderer>(p.renderer) ? 1 : 0;
		ImGui::SetNextItemWidth(150.f);
		if (ImGui::BeginCombo("##particle_renderer", rendererNames[current]))
		{
			for (int index = 0; index < 2; ++index)
			{
				if (ImGui::Selectable(rendererNames[index], index == current))
				{
					if (index == 0 && !std::holds_alternative<ParticleBillboardRenderer>(p.renderer))
						p.renderer = ParticleBillboardRenderer{};
					else if (index == 1 && !std::holds_alternative<ParticleMeshRenderer>(p.renderer))
						p.renderer = ParticleMeshRenderer{};
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::TextUnformatted("Renderer");
	}

	if (auto* billboardRenderer = std::get_if<ParticleBillboardRenderer>(&p.renderer))
	{
		{
			static const char* facingNames[] = { "Camera Plane", "Camera Position", "Velocity Aligned", "Axis Locked" };
			const int current = static_cast<int>(billboardRenderer->facing);
			ImGui::SetNextItemWidth(150.f);
			if (ImGui::BeginCombo("##particle_facing", facingNames[current]))
			{
				for (int index = 0; index < 4; ++index)
				{
					if (ImGui::Selectable(facingNames[index], index == current))
					{
						billboardRenderer->facing = static_cast<ParticleFacing>(index);
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			ImGui::TextUnformatted("Facing");
		}

		{
			static const char* maskNames[] = { "None", "Disc", "Soft Disc" };
			const int current = static_cast<int>(billboardRenderer->mask);
			ImGui::SetNextItemWidth(150.f);
			if (ImGui::BeginCombo("##particle_mask", maskNames[current]))
			{
				for (int index = 0; index < 3; ++index)
				{
					if (ImGui::Selectable(maskNames[index], index == current))
					{
						billboardRenderer->mask = static_cast<ParticleMask>(index);
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			ImGui::TextUnformatted("Mask");
		}

		ImGui::TextUnformatted("Material");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1.f);
		ImGui::Button(assetLabel(billboardRenderer->materialGuid, "None (drag a material here)").c_str(), ImVec2(-1.f, 0.f));
		if (acceptDrop(AssetType::Material, billboardRenderer->materialGuid))
		{
			auto asset = assetManager ? assetManager->LoadAsset(billboardRenderer->materialGuid) : nullptr;
			billboardRenderer->material = asset ? asset->AsMaterial() : nullptr;
			changed = true;
		}
		if (!billboardRenderer->materialGuid.empty())
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("X##clear_material"))
			{
				billboardRenderer->materialGuid = AssetGUID();
				billboardRenderer->material.reset();
				changed = true;
			}
		}
	}
	else if (auto* meshRenderer = std::get_if<ParticleMeshRenderer>(&p.renderer))
	{
		// 网格类型：Cube / Sphere 用内置网格，Custom 用用户自己的模型
		{
			static const char* meshTypeNames[] = { "Cube", "Sphere", "Custom" };
			const int current = static_cast<int>(meshRenderer->meshType);
			ImGui::SetNextItemWidth(150.f);
			if (ImGui::BeginCombo("##particle_mesh_type", meshTypeNames[current]))
			{
				for (int index = 0; index < 3; ++index)
				{
					if (ImGui::Selectable(meshTypeNames[index], index == current))
					{
						meshRenderer->meshType = static_cast<ParticleMeshType>(index);
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			ImGui::TextUnformatted("Mesh Type");
		}

		if (meshRenderer->meshType == ParticleMeshType::Custom)
		{
			// Custom：几何与材质都来自模型本身
			ImGui::TextUnformatted("Model");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1.f);
			ImGui::Button(assetLabel(meshRenderer->modelGuid, "None (drag a model here)").c_str(), ImVec2(-1.f, 0.f));
			if (acceptDrop(AssetType::StaticModel, meshRenderer->modelGuid))
			{
				auto asset = assetManager ? assetManager->LoadAsset(meshRenderer->modelGuid) : nullptr;
				meshRenderer->model = asset ? asset->AsStaticModel() : nullptr;
				changed = true;
			}
			if (!meshRenderer->modelGuid.empty())
			{
				ImGui::SameLine();
				if (ImGui::SmallButton("X##clear_model"))
				{
					meshRenderer->modelGuid = AssetGUID();
					meshRenderer->model.reset();
					changed = true;
				}
			}
		}
		else
		{
			// Cube / Sphere：内置网格，材质可选，留空就只用粒子的 tint 颜色
			ImGui::TextUnformatted("Material");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1.f);
			ImGui::Button(assetLabel(meshRenderer->materialGuid, "None (particle tint only)").c_str(), ImVec2(-1.f, 0.f));
			if (acceptDrop(AssetType::Material, meshRenderer->materialGuid))
			{
				auto asset = assetManager ? assetManager->LoadAsset(meshRenderer->materialGuid) : nullptr;
				meshRenderer->material = asset ? asset->AsMaterial() : nullptr;
				changed = true;
			}
			if (!meshRenderer->materialGuid.empty())
			{
				ImGui::SameLine();
				if (ImGui::SmallButton("X##clear_mesh_material"))
				{
					meshRenderer->materialGuid = AssetGUID();
					meshRenderer->material.reset();
					changed = true;
				}
			}
		}
	}

	// 混合模式：加色顺序无关、直出；AlphaBlend 走 OIT 的权重累积，两者可以逐发射器选。
	{
		static const char* blendNames[] = { "Additive", "Alpha Blend (OIT)" };
		const int current = static_cast<int>(p.blendMode);
		ImGui::SetNextItemWidth(150.f);
		if (ImGui::BeginCombo("##particle_blend_mode", blendNames[current]))
		{
			for (int index = 0; index < 2; ++index)
			{
				if (ImGui::Selectable(blendNames[index], index == current))
				{
					p.blendMode = static_cast<ParticleBlendMode>(index);
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::TextUnformatted("Blend");
	}

	// ------------------------------------------------------------ 生成器
	ImGui::TextDisabled("Generators");

	// scaleGenerator
	if (ImGui::TreeNode("Scale Generator"))
	{
		auto* random = dynamic_cast<RandomScaleGenerator*>(p.scaleGenerator.get());
		auto* fixed = dynamic_cast<FixedScaleGenerator*>(p.scaleGenerator.get());
		static const char* kinds[] = { "Random", "Fixed" };
		const int current = random ? 0 : (fixed ? 1 : -1);
		const int picked = kindCombo("##scale_kind", kinds, 2, current);
		if (picked >= 0 && picked != current)
		{
			if (picked == 0)
				p.scaleGenerator = std::make_shared<RandomScaleGenerator>(glm::vec3(0.05f), glm::vec3(0.15f));
			else
				p.scaleGenerator = std::make_shared<FixedScaleGenerator>(glm::vec3(0.1f));
			random = dynamic_cast<RandomScaleGenerator*>(p.scaleGenerator.get());
			fixed = dynamic_cast<FixedScaleGenerator*>(p.scaleGenerator.get());
			changed = true;
		}
		if (random)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat3("##scale_min", &random->min.x, 0.001f, 0.f, 1000.f);
			ImGui::SameLine();
			ImGui::TextUnformatted("min");
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat3("##scale_max", &random->max.x, 0.001f, 0.f, 1000.f);
			ImGui::SameLine();
			ImGui::TextUnformatted("max");
		}
		else if (fixed)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat3("##scale_value", &fixed->scale.x, 0.001f, 0.f, 1000.f);
			ImGui::SameLine();
			ImGui::TextUnformatted("scale");
		}
		ImGui::TreePop();
	}

	// colorGenerator
	if (ImGui::TreeNode("Color Generator"))
	{
		auto* random = dynamic_cast<RandomColorGenerator*>(p.colorGenerator.get());
		auto* fixed = dynamic_cast<FixedColorGenerator*>(p.colorGenerator.get());
		static const char* kinds[] = { "Random", "Fixed" };
		const int current = random ? 0 : (fixed ? 1 : -1);
		const int picked = kindCombo("##color_kind", kinds, 2, current);
		if (picked >= 0 && picked != current)
		{
			if (picked == 0)
				p.colorGenerator = std::make_shared<RandomColorGenerator>(glm::vec3(0.2f), glm::vec3(1.0f));
			else
				p.colorGenerator = std::make_shared<FixedColorGenerator>(glm::vec3(1.0f));
			random = dynamic_cast<RandomColorGenerator*>(p.colorGenerator.get());
			fixed = dynamic_cast<FixedColorGenerator*>(p.colorGenerator.get());
			changed = true;
		}
		if (random)
		{
			changed |= ImGui::ColorEdit3("min##color_min", &random->min.x);
			changed |= ImGui::ColorEdit3("max##color_max", &random->max.x);
		}
		else if (fixed)
			changed |= ImGui::ColorEdit3("value##color_value", &fixed->color.x);
		ImGui::TreePop();
	}

	// lifeTimeGenerator
	if (ImGui::TreeNode("Lifetime Generator"))
	{
		auto* random = dynamic_cast<RandomLifeTimeGenerator*>(p.lifeTimeGenerator.get());
		auto* fixed = dynamic_cast<FixedLifeTimeGenerator*>(p.lifeTimeGenerator.get());
		static const char* kinds[] = { "Random", "Fixed" };
		const int current = random ? 0 : (fixed ? 1 : -1);
		const int picked = kindCombo("##life_kind", kinds, 2, current);
		if (picked >= 0 && picked != current)
		{
			if (picked == 0)
				p.lifeTimeGenerator = std::make_shared<RandomLifeTimeGenerator>(0.8f, 1.6f);
			else
				p.lifeTimeGenerator = std::make_shared<FixedLifeTimeGenerator>(2.0f);
			random = dynamic_cast<RandomLifeTimeGenerator*>(p.lifeTimeGenerator.get());
			fixed = dynamic_cast<FixedLifeTimeGenerator*>(p.lifeTimeGenerator.get());
			changed = true;
		}
		if (random)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat("min##life_min", &random->min, 0.01f, 0.01f, 1000.f);
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat("max##life_max", &random->max, 0.01f, 0.01f, 1000.f);
		}
		else if (fixed)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat("lifetime##life_value", &fixed->lifetime, 0.01f, 0.01f, 1000.f);
		}
		ImGui::TreePop();
	}

	// opacityGenerator
	if (ImGui::TreeNode("Opacity Generator"))
	{
		auto* random = dynamic_cast<RandomOpacityGenerator*>(p.opacityGenerator.get());
		auto* fixed = dynamic_cast<FixedOpactiyGenerator*>(p.opacityGenerator.get());
		static const char* kinds[] = { "Random", "Fixed" };
		const int current = random ? 0 : (fixed ? 1 : -1);
		const int picked = kindCombo("##opacity_kind", kinds, 2, current);
		if (picked >= 0 && picked != current)
		{
			if (picked == 0)
				p.opacityGenerator = std::make_shared<RandomOpacityGenerator>(0.5f, 1.0f);
			else
				p.opacityGenerator = std::make_shared<FixedOpactiyGenerator>(1.0f);
			random = dynamic_cast<RandomOpacityGenerator*>(p.opacityGenerator.get());
			fixed = dynamic_cast<FixedOpactiyGenerator*>(p.opacityGenerator.get());
			changed = true;
		}
		if (random)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::SliderFloat("min##opacity_min", &random->min, 0.f, 1.f);
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::SliderFloat("max##opacity_max", &random->max, 0.f, 1.f);
		}
		else if (fixed)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::SliderFloat("opacity##opacity_value", &fixed->opacity, 0.f, 1.f);
		}
		ImGui::TreePop();
	}

	// positionGenerator
	if (ImGui::TreeNode("Position Generator"))
	{
		auto* spherical = dynamic_cast<SphericalGenerator*>(p.posGenerator.get());
		auto* ball = dynamic_cast<BallGenerator*>(p.posGenerator.get());
		auto* box = dynamic_cast<BoxGenerator*>(p.posGenerator.get());
		const bool point = p.posGenerator && !spherical && !ball && !box;
		static const char* kinds[] = { "Point", "Spherical", "Ball", "Box" };
		const int current = point ? 0 : (spherical ? 1 : (ball ? 2 : (box ? 3 : -1)));
		const int picked = kindCombo("##pos_kind", kinds, 4, current);
		if (picked >= 0 && picked != current)
		{
			switch (picked)
			{
			case 0: p.posGenerator = std::make_shared<PointGenerator>(); break;
			case 1: p.posGenerator = std::make_shared<SphericalGenerator>(1.0f); break;
			case 2: p.posGenerator = std::make_shared<BallGenerator>(1.0f); break;
			default: p.posGenerator = std::make_shared<BoxGenerator>(glm::vec3(1.0f)); break;
			}
			spherical = dynamic_cast<SphericalGenerator*>(p.posGenerator.get());
			ball = dynamic_cast<BallGenerator*>(p.posGenerator.get());
			box = dynamic_cast<BoxGenerator*>(p.posGenerator.get());
			changed = true;
		}
		if (spherical || ball)
		{
			float* radius = spherical ? &spherical->radius : &ball->radius;
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat("radius##pos_radius", radius, 0.01f, 0.f, 1000.f);
		}
		else if (box)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat3("half size##pos_half", &box->halfSize.x, 0.01f, 0.f, 1000.f);
		}
		ImGui::TreePop();
	}

	// directionGenerator
	if (ImGui::TreeNode("Direction Generator"))
	{
		auto* fixedCone = dynamic_cast<FixedDirectionConeGenerator*>(p.dirGenerator.get());
		auto* follow = dynamic_cast<FollowEmitterDirectionConeGenerator*>(p.dirGenerator.get());
		auto* radial = dynamic_cast<RadialDirectionGenerator*>(p.dirGenerator.get());
		static const char* kinds[] = { "Fixed Cone", "Emitter Cone", "Radial" };
		const int current = fixedCone ? 0 : (follow ? 1 : (radial ? 2 : -1));
		const int picked = kindCombo("##dir_kind", kinds, 3, current);
		if (picked >= 0 && picked != current)
		{
			switch (picked)
			{
			case 0: p.dirGenerator = std::make_shared<FixedDirectionConeGenerator>(glm::vec3(0.f, 0.f, -1.f), 0.2f); break;
			case 1: p.dirGenerator = std::make_shared<FollowEmitterDirectionConeGenerator>(0.3f); break;
			default: p.dirGenerator = std::make_shared<RadialDirectionGenerator>(0.3f); break;
			}
			fixedCone = dynamic_cast<FixedDirectionConeGenerator*>(p.dirGenerator.get());
			follow = dynamic_cast<FollowEmitterDirectionConeGenerator*>(p.dirGenerator.get());
			radial = dynamic_cast<RadialDirectionGenerator*>(p.dirGenerator.get());
			changed = true;
		}
		if (fixedCone)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat3("direction##dir_value", &fixedCone->direction.x, 0.01f, -1.f, 1.f);
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::SliderFloat("spread##dir_spread", &fixedCone->spreadAngle, 0.f, 3.14159f);
		}
		else if (follow)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::SliderFloat("spread##dir_spread_follow", &follow->spreadAngle, 0.f, 3.14159f);
		}
		else if (radial)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::SliderFloat("spread##dir_spread_radial", &radial->spreadAngle, 0.f, 3.14159f);
		}
		ImGui::TreePop();
	}

	// rotationGenerator
	if (ImGui::TreeNode("Rotation Generator"))
	{
		auto* fixedRotation = dynamic_cast<FixedRotationGenerator*>(p.rotGenerator.get());
		auto* followEmitter = dynamic_cast<FollowEmitterRotationGenerator*>(p.rotGenerator.get());
		auto* followVelocity = dynamic_cast<FollowVelocityRotationGenerator*>(p.rotGenerator.get());
		static const char* kinds[] = { "Fixed", "Emitter", "Velocity" };
		const int current = fixedRotation ? 0 : (followEmitter ? 1 : (followVelocity ? 2 : -1));
		const int picked = kindCombo("##rot_kind", kinds, 3, current);
		if (picked >= 0 && picked != current)
		{
			switch (picked)
			{
			case 0: p.rotGenerator = std::make_shared<FixedRotationGenerator>(glm::identity<glm::quat>()); break;
			case 1: p.rotGenerator = std::make_shared<FollowEmitterRotationGenerator>(); break;
			default: p.rotGenerator = std::make_shared<FollowVelocityRotationGenerator>(); break;
			}
			fixedRotation = dynamic_cast<FixedRotationGenerator*>(p.rotGenerator.get());
			changed = true;
		}
		if (fixedRotation)
		{
			float quat[4] = { fixedRotation->rotation.w, fixedRotation->rotation.x, fixedRotation->rotation.y, fixedRotation->rotation.z };
			ImGui::SetNextItemWidth(220.f);
			if (ImGui::DragFloat4("quat##rot_value", quat, 0.01f, -1.f, 1.f))
			{
				fixedRotation->rotation = glm::normalize(glm::quat(quat[0], quat[1], quat[2], quat[3]));
				changed = true;
			}
		}
		ImGui::TreePop();
	}

	// speedGenerator
	if (ImGui::TreeNode("Speed Generator"))
	{
		auto* random = dynamic_cast<RandomSpeedGenerator*>(p.speedGenerator.get());
		auto* fixed = dynamic_cast<FixedSpeedGenerator*>(p.speedGenerator.get());
		static const char* kinds[] = { "Random", "Fixed" };
		const int current = random ? 0 : (fixed ? 1 : -1);
		const int picked = kindCombo("##speed_kind", kinds, 2, current);
		if (picked >= 0 && picked != current)
		{
			if (picked == 0)
				p.speedGenerator = std::make_shared<RandomSpeedGenerator>(0.5f, 1.5f);
			else
				p.speedGenerator = std::make_shared<FixedSpeedGenerator>(1.0f);
			random = dynamic_cast<RandomSpeedGenerator*>(p.speedGenerator.get());
			fixed = dynamic_cast<FixedSpeedGenerator*>(p.speedGenerator.get());
			changed = true;
		}
		if (random)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat("min##speed_min", &random->min, 0.01f, -1000.f, 1000.f);
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat("max##speed_max", &random->max, 0.01f, -1000.f, 1000.f);
		}
		else if (fixed)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat("speed##speed_value", &fixed->speed, 0.01f, -1000.f, 1000.f);
		}
		ImGui::TreePop();
	}

	// positionOffsetGenerator
	if (ImGui::TreeNode("Position Offset Generator"))
	{
		auto* fixed = dynamic_cast<FixedPositionOffsetGenerator*>(p.posOffsetGenerator.get());
		auto* radial = dynamic_cast<RadialPositionOffsetGenerator*>(p.posOffsetGenerator.get());
		static const char* kinds[] = { "Fixed", "Radial" };
		const int current = fixed ? 0 : (radial ? 1 : -1);
		const int picked = kindCombo("##offset_kind", kinds, 2, current);
		if (picked >= 0 && picked != current)
		{
			if (picked == 0)
				p.posOffsetGenerator = std::make_shared<FixedPositionOffsetGenerator>(glm::vec3(0.0f));
			else
				p.posOffsetGenerator = std::make_shared<RadialPositionOffsetGenerator>();
			fixed = dynamic_cast<FixedPositionOffsetGenerator*>(p.posOffsetGenerator.get());
			changed = true;
		}
		if (fixed)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat3("offset##offset_value", &fixed->offset.x, 0.01f, -1000.f, 1000.f);
		}
		ImGui::TreePop();
	}

	// ------------------------------------------------------------ 更新器
	ImGui::TextDisabled("Updaters");

	// opacityUpdater
	if (ImGui::TreeNode("Opacity Updater"))
	{
		auto* linear = dynamic_cast<LinearOpacityUpdater*>(p.opacityUpdater.get());
		static const char* kinds[] = { "None", "Linear" };
		const int current = linear ? 1 : 0;
		const int picked = kindCombo("##opacityupd_kind", kinds, 2, current);
		if (picked >= 0 && picked != current)
		{
			if (picked == 0)
				p.opacityUpdater.reset();
			else
				p.opacityUpdater = std::make_shared<LinearOpacityUpdater>(0.0f);
			linear = dynamic_cast<LinearOpacityUpdater*>(p.opacityUpdater.get());
			changed = true;
		}
		if (linear)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::SliderFloat("goal##opacityupd_goal", &linear->goalOpacity, 0.f, 1.f);
		}
		ImGui::TreePop();
	}

	// baseColorUpdater
	if (ImGui::TreeNode("Base Color Updater"))
	{
		auto* linear = dynamic_cast<LinearBaseColorUpdater*>(p.baseColorUpdater.get());
		auto* fire = dynamic_cast<FireColorUpdater*>(p.baseColorUpdater.get());
		static const char* kinds[] = { "None", "Linear", "Fire" };
		const int current = linear ? 1 : (fire ? 2 : 0);
		const int picked = kindCombo("##colorupd_kind", kinds, 3, current);
		if (picked >= 0 && picked != current)
		{
			if (picked == 0)
				p.baseColorUpdater.reset();
			else if (picked == 1)
				p.baseColorUpdater = std::make_shared<LinearBaseColorUpdater>(glm::vec3(1.0f));
			else
				p.baseColorUpdater = std::make_shared<FireColorUpdater>();
			linear = dynamic_cast<LinearBaseColorUpdater*>(p.baseColorUpdater.get());
			fire = dynamic_cast<FireColorUpdater*>(p.baseColorUpdater.get());
			changed = true;
		}
		if (linear)
			changed |= ImGui::ColorEdit3("goal##colorupd_goal", &linear->goalColor.x);
		else if (fire)
		{
			changed |= ImGui::ColorEdit3("inner##fire_inner", &fire->innerColor.x);
			changed |= ImGui::ColorEdit3("mid##fire_mid", &fire->midColor.x);
			changed |= ImGui::ColorEdit3("outer##fire_outer", &fire->outerColor.x);
		}
		ImGui::TreePop();
	}

	// scaleUpdater
	if (ImGui::TreeNode("Scale Updater"))
	{
		auto* linear = dynamic_cast<LinearScaleUpdater*>(p.scaleUpdater.get());
		static const char* kinds[] = { "None", "Linear" };
		const int current = linear ? 1 : 0;
		const int picked = kindCombo("##scaleupd_kind", kinds, 2, current);
		if (picked >= 0 && picked != current)
		{
			if (picked == 0)
				p.scaleUpdater.reset();
			else
				p.scaleUpdater = std::make_shared<LinearScaleUpdater>(glm::vec3(0.1f));
			linear = dynamic_cast<LinearScaleUpdater*>(p.scaleUpdater.get());
			changed = true;
		}
		if (linear)
		{
			ImGui::SetNextItemWidth(150.f);
			changed |= ImGui::DragFloat3("goal##scaleupd_goal", &linear->goalScale.x, 0.001f, 0.f, 1000.f);
		}
		ImGui::TreePop();
	}

	// rotationUpdater
	if (ImGui::TreeNode("Rotation Updater"))
	{
		auto* follow = dynamic_cast<FollowVelocityRotationUpdater*>(p.rotationUpdater.get());
		static const char* kinds[] = { "None", "Follow Velocity" };
		const int current = follow ? 1 : 0;
		const int picked = kindCombo("##rotupd_kind", kinds, 2, current);
		if (picked >= 0 && picked != current)
		{
			if (picked == 0)
				p.rotationUpdater.reset();
			else
				p.rotationUpdater = std::make_shared<FollowVelocityRotationUpdater>();
			changed = true;
		}
		ImGui::TreePop();
	}

	// 局部/世界加速度：同一套 5 种，None = 空指针。
	const auto drawAccelerationUpdater = [&](const char* label, std::shared_ptr<IAccelerationUpdater>& slot)
		{
			auto* gravity = dynamic_cast<GravityAccelerationUpdater*>(slot.get());
			auto* linear = dynamic_cast<LinearAccelerationUpdater*>(slot.get());
			auto* perlin = dynamic_cast<PerlinTurbulenceUpdater*>(slot.get());
			auto* fixed = (gravity || linear || perlin) ? nullptr : dynamic_cast<FiexedAccelerationUpdater*>(slot.get());
			static const char* kinds[] = { "None", "Fixed", "Gravity", "Linear", "Perlin" };
			const int current = slot == nullptr ? 0 : (fixed ? 1 : (gravity ? 2 : (linear ? 3 : (perlin ? 4 : -1))));
			const int picked = kindCombo(label, kinds, 5, current);
			bool localChanged = false;
			if (picked >= 0 && picked != current)
			{
				switch (picked)
				{
				case 0: slot.reset(); break;
				case 1: slot = std::make_shared<FiexedAccelerationUpdater>(glm::vec3(0.f, -9.8f, 0.f)); break;
				case 2: slot = std::make_shared<GravityAccelerationUpdater>(); break;
				case 3: slot = std::make_shared<LinearAccelerationUpdater>(glm::vec3(0.f, 1.f, 0.f), 0.f, 9.8f); break;
				default: slot = std::make_shared<PerlinTurbulenceUpdater>(0.5f, 1.5f); break;
				}
				gravity = dynamic_cast<GravityAccelerationUpdater*>(slot.get());
				linear = dynamic_cast<LinearAccelerationUpdater*>(slot.get());
				perlin = dynamic_cast<PerlinTurbulenceUpdater*>(slot.get());
				fixed = (gravity || linear || perlin) ? nullptr : dynamic_cast<FiexedAccelerationUpdater*>(slot.get());
				localChanged = true;
			}
			if (fixed && !gravity)
			{
				ImGui::SetNextItemWidth(150.f);
				localChanged |= ImGui::DragFloat3("acceleration##acc_value", &fixed->acceleration.x, 0.01f, -1000.f, 1000.f);
			}
			else if (gravity)
				ImGui::TextDisabled("acceleration = %s", std::format("({:.2f}, {:.2f}, {:.2f})", gravity->acceleration.x, gravity->acceleration.y, gravity->acceleration.z).c_str());
			else if (linear)
			{
				ImGui::SetNextItemWidth(150.f);
				localChanged |= ImGui::DragFloat3("dir##acc_dir", &linear->accelerationDir.x, 0.01f, -1.f, 1.f);
				ImGui::SetNextItemWidth(150.f);
				localChanged |= ImGui::DragFloat("init##acc_init", &linear->initAccelerationValue, 0.01f, -1000.f, 1000.f);
				ImGui::SetNextItemWidth(150.f);
				localChanged |= ImGui::DragFloat("goal##acc_goal", &linear->goalAccelerationValue, 0.01f, -1000.f, 1000.f);
			}
			else if (perlin)
			{
				ImGui::SetNextItemWidth(150.f);
				localChanged |= ImGui::DragFloat("strength##perlin_strength", &perlin->strength, 0.01f, 0.f, 100.f);
				ImGui::SetNextItemWidth(150.f);
				localChanged |= ImGui::DragFloat("frequency##perlin_freq", &perlin->frequency, 0.01f, 0.f, 100.f);
			}
			return localChanged;
		};

	// localAccelerationUpdater
	if (ImGui::TreeNode("Local Acceleration Updater"))
	{
		changed |= drawAccelerationUpdater("##local_acc_kind", p.localAccelerationUpdater);
		ImGui::TreePop();
	}

	// worldAccelerationUpdater
	if (ImGui::TreeNode("World Acceleration Updater"))
	{
		changed |= drawAccelerationUpdater("##world_acc_kind", p.worldAccelerationUpdater);
		ImGui::TreePop();
	}

	ImGui::Spacing();
	ImGui::SameLine();
	if (!p.IsVaild())
		ImGui::TextColored(ImVec4(1.f, 0.55f, 0.35f, 1.f), "Generators incomplete - nothing will emit");

	return changed;
}

template<>
inline bool PropertiesHelper::DrawComponent<Script>(Script& comp, ProjectManager* assetManager)
{
	bool changed = false;
	if (!comp.data)
	{
		ImGui::TextDisabled("No Script assigned");
	}
	else
	{
		const auto drawAssetPath = [assetManager](const char* label, const AssetGUID& guid)
			{
				ImGui::Text("%s", label);
				ImGui::SameLine();
				if (guid.empty())
				{
					ImGui::TextDisabled("Unlinked");
					return;
				}

				auto database = assetManager ? assetManager->GetDatabase() : nullptr;
				auto meta = database ? database->GetAssetMetaByGuid(guid) : nullptr;
				if (!meta)
				{
					ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Missing asset (%s)", guid.c_str());
					return;
				}

				ImGui::TextUnformatted(meta->name.c_str());
				ImGui::TextDisabled("%s", meta->path.c_str());
			};

		drawAssetPath("ScriptSource", comp.data->scriptGuid);
	}

	changed |= ImGui::Checkbox("Enabled", &comp.enabled);
	ImGui::SameLine();
	if (ImGui::Button("Reload"))
	{
		auto reloadScript = [&]() -> bool
			{
				if (!assetManager || !comp.data || comp.data->scriptGuid.empty())
					return false;

				auto& guid = comp.data->scriptGuid;
				if (auto meta = assetManager->GetDatabase()->GetAssetMetaByGuid(guid))
				{
					if (meta->type != AssetType::Script)
						return false;

					auto asset = assetManager->LoadAsset(guid);
					auto source = asset ? asset->AsScript() : nullptr;
					if (!source)
						return false;

					comp.data = source;
					return true;
				}
			};

		reloadScript();
		comp.needReload = true;
		changed |= true;
	}
	if (ImGui::Button("Drop script asset to replace", ImVec2(-FLT_MIN, 0.0f))) {}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID"))
		{
			const char* data = static_cast<const char*>(payload->Data);
			if (data && *data && assetManager)
			{
				auto database = assetManager->GetDatabase();
				auto meta = database ? database->GetAssetMetaByGuid(AssetGUID(data)) : nullptr;
				if (meta && meta->type == AssetType::Script)
				{
					auto asset = assetManager->LoadAsset(meta->guid);
					auto source = asset ? asset->AsScript() : nullptr;
					if (source)
					{
						comp.data = std::move(source);
						comp.needReload = true;
						changed = true;
					}
				}
			}
		}
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
		{
			const char* data = static_cast<const char*>(payload->Data);
			if (data && strlen(data) > 0)
			{
				std::string fileFullPath(data);
				std::string ext = fs::path(fileFullPath).extension().string();
				if (ext == ".lua")
				{
					if (assetManager)
						ImguiLayout::HandleScriptFileDrop(fileFullPath, ImguiLayout::selectedEntity, assetManager);
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

	return changed;
}

template<>
inline bool PropertiesHelper::DrawComponent<UIView>(UIView& comp, ProjectManager* assetManager)
{
	bool changed = false;

	auto database = assetManager ? assetManager->GetDatabase() : nullptr;
	auto meta = (database && !comp.documentGuid.empty()) ? database->GetAssetMetaByGuid(comp.documentGuid) : nullptr;

	ImGui::TextUnformatted("UI Document");
	ImGui::SameLine();
	if (comp.documentGuid.empty())
	{
		ImGui::TextDisabled("Unlinked");
	}
	else if (!meta)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Missing asset (%s)", comp.documentGuid.c_str());
	}
	else
	{
		ImGui::TextUnformatted(meta->name.c_str());
		ImGui::TextDisabled("%s", meta->path.c_str());
	}

	{
		char buffer[256]{};
		strncpy_s(buffer, comp.viewName.c_str(), _TRUNCATE);
		SetNextPropertyFieldWidth(130.0f, 0.55f);
		if (ImGui::InputText("View Name", buffer, sizeof(buffer), ImGuiInputTextFlags_AutoSelectAll))
		{
			comp.viewName = buffer;
			changed = true;
		}
		if (comp.viewName.empty())
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Unset View Name can't be find by module");
		}
	}

	changed |= ImGui::Checkbox("Enabled", &comp.enabled);
	changed |= ImGui::Checkbox("Captures Input", &comp.capturesInput);
	SetNextPropertyFieldWidth(130.0f, 0.55f);
	changed |= ImGui::DragInt("Layer", &comp.layer, 0.1f, -10000, 10000);
	if (ImGui::Button("Reload"))
	{
		comp.needReload = true;
		changed = true;
	}

	// 和脚本组件一样：可以把资产面板里的 .ui 拖到这个按钮上替换。
	if (ImGui::Button("Drop UI document to replace", ImVec2(-FLT_MIN, 0.0f))) {}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID"))
		{
			const char* data = static_cast<const char*>(payload->Data);
			if (data && *data && assetManager)
			{
				auto droppedMeta = database ? database->GetAssetMetaByGuid(AssetGUID(data)) : nullptr;
				if (droppedMeta && droppedMeta->type == AssetType::UiDocument)
				{
					comp.documentGuid = droppedMeta->guid;
					comp.needReload = true;
					changed = true;
				}
			}
		}
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
		{
			const char* data = static_cast<const char*>(payload->Data);
			if (data && strlen(data) > 0)
			{
				std::string fileFullPath(data);
				std::string ext = fs::path(fileFullPath).extension().string();
				if (ext == ".ui" && assetManager)
					ImguiLayout::HandleUIAssetFileDrop(fileFullPath, ImguiLayout::selectedEntity, assetManager);
			}
		}
		ImGui::EndDragDropTarget();
	}

	return changed;
}

namespace PropertiesHelperDetail
{
	inline const char* BlendModeNames[] = { "Alpha（透明混合）", "Additive（叠加）", "Multiply（正片叠底）", "Premultiplied（预乘 Alpha）" };

	inline bool DrawBlendMode(VKRenderContext::BlendMode2D& blend)
	{
		int value = static_cast<int>(blend);
		PropertiesHelper::SetNextPropertyFieldWidth(160.0f, 0.55f);
		if (ImGui::Combo("混合模式", &value, BlendModeNames, IM_ARRAYSIZE(BlendModeNames)))
		{
			blend = static_cast<VKRenderContext::BlendMode2D>(value);
			return true;
		}
		return false;
	}

	// RenderBase2D 的公共字段：可见、层级、层内顺序、偏移
	inline bool DrawRenderBase2D(RenderBase2D& comp)
	{
		bool changed = false;
		changed |= ImGui::Checkbox("可见", &comp.visible);
		PropertiesHelper::SetNextPropertyFieldWidth(130.0f, 0.55f);
		changed |= ImGui::DragInt("层级", &comp.layer, 0.1f, -100000, 100000);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("大的后画（盖在上面）；编辑器点选时也优先选层级大的");
		PropertiesHelper::SetNextPropertyFieldWidth(130.0f, 0.55f);
		changed |= ImGui::DragInt("层内顺序", &comp.internalZOrder, 0.1f, -100000, 100000);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("同一层级内的先后，大的后画");
		PropertiesHelper::SetNextPropertyFieldWidth(250.0f, 0.6f);
		changed |= ImGui::DragFloat2("偏移", glm::value_ptr(comp.offset), 0.5f);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("渲染中心相对 Transform2D 的偏移（随物体旋转）");
		return changed;
	}

	inline bool DrawSizeField(glm::vec2& size, const glm::vec2& native)
	{
		bool changed = false;
		PropertiesHelper::SetNextPropertyFieldWidth(250.0f, 0.6f);
		changed |= ImGui::DragFloat2("尺寸", glm::value_ptr(size), 0.5f, 0.0f, 100000.0f);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("世界尺寸；分量为 0 时用图像像素尺寸（1 像素 = 1 单位）");
		ImGui::SameLine();
		if (ImGui::SmallButton("原始尺寸") && native.x > 0.0f && native.y > 0.0f)
		{
			size = native;
			changed = true;
		}
		return changed;
	}
}

template<>
inline bool PropertiesHelper::DrawComponent<Transform2D>(Transform2D& comp, ProjectManager* assetManager)
{
	bool changed = false;
	SetNextPropertyFieldWidth(250.0f, 0.6f);
	changed |= ImGui::DragFloat2("位置", glm::value_ptr(comp.position), 0.5f);
	SetNextPropertyFieldWidth(130.0f, 0.55f);
	changed |= ImGui::DragFloat("旋转", &comp.rotation, 0.5f, -36000.0f, 36000.0f, "%.1f°");
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("单位：度，正方向顺时针（2D 世界 y 朝下）");
	SetNextPropertyFieldWidth(250.0f, 0.6f);
	changed |= ImGui::DragFloat2("缩放", glm::value_ptr(comp.scale), 0.01f);
	return changed;
}

template<>
inline bool PropertiesHelper::DrawComponent<Sprite>(Sprite& comp, ProjectManager* assetManager)
{
	using namespace PropertiesHelperDetail;
	bool changed = false;

	// 拖进来的是贴图：先用贴图的原图生成精灵（走导入弹窗，可以改配置），导入完成后再填给这个组件
	const Entity target = ImguiLayout::selectedEntity;
	changed |= Sprite2DEditor::DrawAssetSlot("精灵", comp.spriteGuid, { AssetType::Sprite }, assetManager,
		{ AssetType::Texture }, [target, assetManager](const AssetMeta& texture)
		{
			ImguiLayout::RequestSpriteFromTexture(texture.guid, assetManager, [target](const AssetMeta& created) mutable
				{
					auto* world = target.getWorld();
					if (!world)
						return;
					world->SubmitCommand([target, guid = created.guid](World& w) mutable
						{
							if (!w.IsValidWorldEntity(target))
								return;
							if (auto* sprite = target.tryGetComponent<Sprite>())
								sprite->spriteGuid = guid;
						});
				});
		});

	auto database = assetManager ? assetManager->GetDatabase() : nullptr;
	auto meta = (database && !comp.spriteGuid.empty()) ? database->GetAssetMetaByGuid(comp.spriteGuid) : nullptr;
	if (meta && meta->type == AssetType::Sprite)
	{
		if (ImGui::Button("编辑精灵资产", ImVec2(-FLT_MIN, 0.0f)))
			Sprite2DEditor::OpenSpriteEditor(meta->guid);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("编辑区域、轴心、九宫格边距和导入配置");

		auto asset = assetManager->LoadAsset(meta->guid);
		auto sprite = asset ? asset->AsSprite() : nullptr;
		const auto names = sprite ? sprite->GetRegionNames() : std::vector<std::string>{};
		const std::string preview = comp.region.empty() ? std::string("（整张图）") : comp.region;
		SetNextPropertyFieldWidth(200.0f, 0.6f);
		if (ImGui::BeginCombo("区域", preview.c_str()))
		{
			if (ImGui::Selectable("（整张图）", comp.region.empty()))
			{
				comp.region.clear();
				changed = true;
			}
			for (const auto& name : names)
			{
				if (ImGui::Selectable(name.c_str(), comp.region == name))
				{
					comp.region = name;
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("一张图里可以切出多块命名区域（序列帧、图集里的按钮等），这里选画哪一块");
		if (!comp.region.empty() && sprite && !sprite->HasRegion(comp.region))
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "区域 \"%s\" 不存在，按整张图画", comp.region.c_str());
	}

	const Render2D::SpriteFrame frame = comp.ResolveFrame();
	if (comp.resolveFailed)
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "资产解析失败（不是精灵资产，或图片加载失败）");
	Sprite2DEditor::DrawFramePreview(frame, ImVec2(96.0f, 96.0f));
	if (frame.IsValid())
	{
		ImGui::SameLine();
		ImGui::TextDisabled("%.0f x %.0f", frame.size.x, frame.size.y);
	}

	changed |= DrawRenderBase2D(comp);
	changed |= DrawSizeField(comp.size, frame.size);

	changed |= ImGui::Checkbox("覆盖轴心", &comp.overridePivot);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("不勾：用精灵区域里设置的轴心");
	if (comp.overridePivot)
	{
		SetNextPropertyFieldWidth(250.0f, 0.6f);
		changed |= ImGui::DragFloat2("轴心", glm::value_ptr(comp.pivot), 0.01f, 0.0f, 1.0f);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("图片上的锚点，按比例：(0,0) 左上，(0.5,0.5) 中心，(0.5,1) 底边中点。\n位置指的就是轴心所在的点，旋转和缩放也绕它进行");
	}

	SetNextPropertyFieldWidth(250.0f, 0.6f);
	changed |= ImGui::ColorEdit4("染色", glm::value_ptr(comp.color));
	changed |= ImGui::Checkbox("水平翻转", &comp.flipX);
	ImGui::SameLine();
	changed |= ImGui::Checkbox("垂直翻转", &comp.flipY);

	int drawMode = static_cast<int>(comp.drawMode);
	const char* drawModes[] = { "简单", "九宫格" };
	SetNextPropertyFieldWidth(160.0f, 0.55f);
	if (ImGui::Combo("绘制模式", &drawMode, drawModes, IM_ARRAYSIZE(drawModes)))
	{
		comp.drawMode = static_cast<SpriteDrawMode>(drawMode);
		changed = true;
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("简单：整块拉伸到尺寸。\n九宫格：按区域的四个边距切成 3x3，四角不缩放、四边单向拉伸、中间双向拉伸，\n适合按钮、面板这类怎么拉伸边框和圆角都不变形的图");
	if (comp.drawMode == SpriteDrawMode::Sliced && frame.IsValid() && !frame.HasBorder())
		ImGui::TextDisabled("这个区域没有设置九宫格边距，效果等同简单模式");

	changed |= DrawBlendMode(comp.blend);
	return changed;
}

template<>
inline bool PropertiesHelper::DrawComponent<AnimatedImagePlayer>(AnimatedImagePlayer& comp, ProjectManager* assetManager)
{
	using namespace PropertiesHelperDetail;
	bool changed = false;

	changed |= Sprite2DEditor::DrawAssetSlot("动图", comp.imageGuid, { AssetType::AnimatedImage }, assetManager);
	if (!comp.imageGuid.empty())
	{
		if (ImGui::Button("编辑动图资产", ImVec2(-FLT_MIN, 0.0f)))
			Sprite2DEditor::OpenAnimatedImageViewer(comp.imageGuid);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("逐帧预览、修改导入配置");
	}

	const Render2D::SpriteFrame frame = comp.ResolveFrame();
	if (comp.resolveFailed)
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "资产解析失败（不是动图，或解码失败）");
	Sprite2DEditor::DrawFramePreview(frame, ImVec2(96.0f, 96.0f));
	if (comp.resource)
	{
		ImGui::SameLine();
		ImGui::BeginGroup();
		ImGui::TextDisabled("%u 帧，一轮 %.0f ms", comp.resource->GetFrameCount(), comp.resource->GetTotalDuration());
		ImGui::TextDisabled("当前第 %u 帧，%.0f ms%s", comp.resource->FrameIndexAt(comp.time, comp.loopCount), comp.time, comp.finished ? "（已播完）" : "");
		ImGui::EndGroup();
	}

	// ---- 播放方式（只在组件上，资产不带） ----
	changed |= ImGui::Checkbox("播放中", &comp.playing);
	ImGui::SameLine();
	if (ImGui::SmallButton("从头播放"))
	{
		comp.restartRequested = true;
		comp.playing = true;
		changed = true;
	}
	SetNextPropertyFieldWidth(130.0f, 0.55f);
	changed |= ImGui::DragInt("循环次数", &comp.loopCount, 0.1f, 0, 100000);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = 无限循环；大于 0 时播完停在最后一帧");
	SetNextPropertyFieldWidth(130.0f, 0.55f);
	changed |= ImGui::DragFloat("速度", &comp.speed, 0.01f, 0.0f, 100.0f, "%.2fx");
	ImGui::TextDisabled("编辑模式下不推进时间，进入运行后开始播放");

	changed |= DrawRenderBase2D(comp);
	changed |= DrawSizeField(comp.size, frame.size);
	SetNextPropertyFieldWidth(250.0f, 0.6f);
	changed |= ImGui::DragFloat2("轴心", glm::value_ptr(comp.pivot), 0.01f, 0.0f, 1.0f);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("图片上的锚点，按比例：(0,0) 左上，(0.5,0.5) 中心，(0.5,1) 底边中点。\n位置指的就是轴心所在的点，旋转和缩放也绕它进行");
	SetNextPropertyFieldWidth(250.0f, 0.6f);
	changed |= ImGui::ColorEdit4("染色", glm::value_ptr(comp.color));
	changed |= ImGui::Checkbox("水平翻转", &comp.flipX);
	ImGui::SameLine();
	changed |= ImGui::Checkbox("垂直翻转", &comp.flipY);
	changed |= DrawBlendMode(comp.blend);
	return changed;
}

template<>
inline bool PropertiesHelper::DrawComponent<Camera2D>(Camera2D& comp, ProjectManager* assetManager)
{
	bool changed = false;
	SetNextPropertyFieldWidth(250.0f, 0.6f);
	changed |= ImGui::DragFloat2("设计尺寸", glm::value_ptr(comp.designSize), 1.0f, 1.0f, 100000.0f);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("设计分辨率（世界单位）。视野中心取同一实体的 Transform2D，没有就在设计区域中央");
	comp.designSize = glm::max(comp.designSize, glm::vec2(1.0f));
	SetNextPropertyFieldWidth(130.0f, 0.55f);
	changed |= ImGui::DragFloat("缩放", &comp.zoom, 0.01f, 0.01f, 100.0f);

	int mode = static_cast<int>(comp.aspectMode);
	const char* modes[] = { "拉伸", "完整显示", "铺满裁切" };
	SetNextPropertyFieldWidth(160.0f, 0.55f);
	if (ImGui::Combo("适配模式", &mode, modes, IM_ARRAYSIZE(modes)))
	{
		comp.aspectMode = static_cast<VKRenderContext::AspectMode2D>(mode);
		changed = true;
	}
	changed |= ImGui::Checkbox("像素对齐", &comp.pixelSnap);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("顶点吸附到整像素");
	changed |= ImGui::Checkbox("主相机", &comp.primary);
	SetNextPropertyFieldWidth(130.0f, 0.55f);
	changed |= ImGui::DragInt("优先级", &comp.priority, 0.1f);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("有多台主相机时取优先级最大的");
	return changed;
}

template<typename T>
void PropertiesHelper::OnComponentChange(Entity entity, T& comp)
{}

template<>
inline void PropertiesHelper::OnComponentChange(Entity entity, Transform& trans)
{
	if (auto* physics = entity.tryGetComponent<Physics>())
		physics->forceSyncTransform = true;
}

template<>
inline void PropertiesHelper::OnComponentChange(Entity entity, Physics& physics)
{
	physics.forceRecalculate = true;
}

template<>
inline bool PropertiesHelper::DrawData(Scene3DRenderOption& option)
{
	bool anyChange = false;

	anyChange |= PropertiesHelper::DrawWithTitle("PostProcessFlags", option.flags);
	anyChange |= PropertiesHelper::DrawWithTitle("PostProcessParams", option.postProcessParams);
	anyChange |= PropertiesHelper::DrawWithTitle("TransparentParams", option.transparentParams);
	anyChange |= PropertiesHelper::DrawWithTitle("AtmosphereParams", option.atmosphereParams);
	anyChange |= PropertiesHelper::DrawWithTitle("SSAOParams", option.ssaoParams);
	anyChange |= PropertiesHelper::DrawWithTitle("ExponentialHeightFog", option.exponentialHeightFogParams);
	anyChange |= PropertiesHelper::DrawWithTitle("VolumetricCloudParams", option.volumetricCloudParams);
	anyChange |= PropertiesHelper::DrawWithTitle("RayTraceGeneralParams", option.rayTraceGeneralParams);
	anyChange |= PropertiesHelper::DrawWithTitle("RayTraceReflectParams", option.rayTraceReflectParams);
	anyChange |= PropertiesHelper::DrawWithTitle("RayTraceGIParams", option.rayTraceGIParams);
	anyChange |= PropertiesHelper::DrawWithTitle("SSReflectParams", option.ssrTraceParams);
	anyChange |= PropertiesHelper::DrawWithTitle("SSGIParams", option.ssgiTraceParams);

	if (ImGui::CollapsingHeader("DDGIParams"))
	{
		ImGui::PushID("DDGIParams");
		// 逐项置灰与模式提示都在 DDGIParams 的 DrawComponent 特化里处理
		anyChange |= PropertiesHelper::DrawData(option.ddgiParams);
		ImGui::PopID();
	}

	return anyChange;
}

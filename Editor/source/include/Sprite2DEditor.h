#pragma once

#include "stdafx.h"
#include "Project/ProjectManager.h"
#include "VulkanRenderEngine/Base/Sprite2D.h"
#include "RenderEngine/ImGuiTextureCache.h"

#include <functional>
#include <initializer_list>

class World;

// 编辑器里和 2D（Sprite / AnimatedImage）相关的界面：
//   - 编辑器自己的 ImGui 贴图缓存（属性面板缩略图、资产浏览器、Sprite 编辑器、动图预览共用）
//   - Sprite 编辑器窗口（编辑区域 rect / pivot / 九宫格边距，改导入配置）
//   - 动图预览窗口（看帧、改导入配置）
namespace Sprite2DEditor
{
	// 编辑器 ImGui 上下文的贴图缓存：主循环每帧 BeginFrame，关闭 ImGui 后端前 Clear
	ImGuiTextureCache& TextureCache();

	// 按比例把一帧画进 [p0, p1]（居中、保持宽高比），NEAREST 过滤的贴图自动切最近邻采样
	void DrawFrameInRect(ImDrawList* drawList, const Render2D::SpriteFrame& frame, const ImVec2& p0, const ImVec2& p1, ImU32 color = IM_COL32_WHITE);
	// 占一块 maxSize 的区域画预览（会推进 ImGui 光标）
	void DrawFramePreview(const Render2D::SpriteFrame& frame, const ImVec2& maxSize);

	// 资产浏览器缩略图：Sprite（默认区域）/ Texture / AnimatedImage（按编辑器时钟播放）。拿不到时返回无效帧
	Render2D::SpriteFrame GetAssetThumbnail(ProjectManager* projectManager, const AssetMeta& meta);

	// 资产槽：显示当前引用、接受从资产浏览器拖进来的指定类型资产；返回是否改了。
	// convertible 里的类型也能拖进来，但不直接填，交给 onConvert（例如贴图先生成精灵，导入完再填）
	bool DrawAssetSlot(const char* label, AssetGUID& guid, std::initializer_list<AssetType> accepted, ProjectManager* projectManager,
		std::initializer_list<AssetType> convertible = {}, const std::function<void(const AssetMeta&)>& onConvert = {});

	// 在 2D 世界 position 处建一个带 Transform2D + Sprite / AnimatedImagePlayer 的实体
	bool Create2DEntity(World& world, const AssetMeta& meta, const glm::vec2& position);

	void OpenSpriteEditor(const AssetGUID& guid);
	void OpenAnimatedImageViewer(const AssetGUID& guid);
	// 每帧调用：画打开着的 Sprite 编辑器 / 动图预览窗口
	void DrawWindows(ProjectManager* projectManager);

	// 编辑器时钟（毫秒），预览动图用
	float EditorTimeMs();
}

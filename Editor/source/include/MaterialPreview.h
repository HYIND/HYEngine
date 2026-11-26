#pragma once

#include "CommonData/AssetGUIDDef.h"

#include <memory>

class Material;

// 材质编辑器里的实时材质预览：用 MaterialPreviewPass 在编辑器主线程上同步渲染。
//   - 只在有变化时才重画：材质版本（属性 / 贴图槽）、贴图内容版本、网格、视角、灯光、曝光、尺寸任一变了；
//     看不见（折叠、滚出视野）时不画。
//   - 输出贴图三张轮流用：编辑器主窗口有帧并行，GPU 可能还在采样上一张，新的一帧写到下一张上。
//   - 显示的是运行时材质的当前状态（含未保存的修改）；只在内存里，不读回 CPU、不写磁盘。
//   - 交互：左键拖动旋转视角，右键拖动旋转灯光，滚轮缩放，双击复位视角；下面一行切换网格和调灯光。
//
// 所有接口只在编辑器主线程（ImGui 线程）调用。
namespace MaterialPreview
{
	// 在当前光标处画 side × side 的预览和下面的工具条。material 为空时只画占位
	void Draw(const AssetGUID& guid, const std::shared_ptr<Material>& material, float side);

	// 关闭材质编辑器时：释放输出贴图（视角、网格、灯光设置保留，下次打开接着用）
	void Reset();

	// 编辑器退出时调用：在 GPU 空闲、关 ImGui 后端（TextureCache().Clear）之前
	void Shutdown();
}

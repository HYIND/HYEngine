#pragma once

#include "stdafx.h"

#include <string_view>

class EditorApp;
struct Scene3DRenderOption;

// 渲染参数面板：Scene3DRenderOption 的各组参数各自一个独立浮动窗口（不参与停靠）。
// 入口有两个，打开的是同一个面板：
//   * 菜单「场景 → 渲染参数」的二级菜单；
//   * 左下角 Options 里 PostProcessFlags 各开关右边的「参数」按钮（只有关联了参数面板的开关才有）。
// 有对应功能开关的面板顶部带同一个开关（与 PostProcessFlags 里的是同一个字段）。
// 绘制沿用 PropertiesHelper 的通用参数绘制，与原来 Options 里的完全一致。
namespace RenderOptionPanels
{
	// 「场景」菜单里的「渲染参数」二级菜单
	void DrawMenu();

	// 每帧调用：绘制打开着的参数面板
	void Draw(EditorApp* app);

	// 左下角 Options 里的 PostProcessFlags：勾选框 + 关联面板的入口按钮。返回是否有改动
	bool DrawFlags(Scene3DRenderOption& option);

	// 按 PostProcessFlags 的字段名打开对应面板（已打开则置前）；没有关联面板返回 false
	bool OpenForFlag(std::string_view flagName);
}

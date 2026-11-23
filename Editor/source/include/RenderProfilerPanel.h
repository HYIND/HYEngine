#pragma once

#include "stdafx.h"

class EditorApp;

// 编辑器「调试 → 渲染性能」窗口：每一帧渲染引擎各步骤与 SceneRenderGraph 各 Pass 的耗时，
// 以及 RenderGraph 的节点图（按依赖分层，颜色 = 耗时）。
// 数据只通过 RenderProfilerChannel 交换（见 VulkanRenderEngine/General/RenderProfiler.h）：
// 窗口关闭（或暂停）时关闭统计，渲染器那边零开销。
namespace RenderProfilerPanel
{
	bool& Visible();

	// 每帧调用；窗口不可见时只负责关闭统计
	void Draw(EditorApp* app);
}

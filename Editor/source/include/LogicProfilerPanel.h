#pragma once

#include "stdafx.h"

class EditorApp;

// 编辑器「调试 → 逻辑性能」窗口：场景线程上 World::update 的耗时，
// 按逻辑 tick 统计每个 System 各阶段的耗时、RenderSystem 收集渲染数据的各段，以及实体 / 粒子规模。
// 数据只通过 LogicProfilerChannel 交换（见 Common ECSCore/LogicProfiler.h）：
// 窗口关闭（或暂停）时关闭统计，场景线程那边零开销。
namespace LogicProfilerPanel
{
	bool& Visible();

	// 每帧调用；窗口不可见时只负责关闭统计
	void Draw(EditorApp* app);
}

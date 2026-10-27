#pragma once

#include "ECSCore/IComponent.h"
#include "CommonData/AssetGUIDDef.h"
#include "UI/UiTypes.h"

#include <memory>

struct UIView : public IComponent
{
	AssetGUID documentGuid;

	// 视图实例名：同一个 .ui 可能被多个实体引用（分屏、两个玩家的 HUD），
	// 所以模块认领界面的键是这个名字，不是文档名。空串 = 无名视图，模块找不到它。
	// 由场景作者在 .scene 里填，或代码创建 UIView 时给；引擎不会自动生成。
	std::string viewName;

	int layer = 0;
	bool capturesInput = true;
	bool enabled = true;
	bool needReload = true;

	UIView() = default;
	UIView(const UIView& other) {
		documentGuid = other.documentGuid;
		viewName = other.viewName;
		layer = other.layer;
		capturesInput = other.capturesInput;
		enabled = other.enabled;
		needReload = true;
	}

	void OnRemove(const Entity& entity) override
	{
		documentGuid.clear();
		viewName.clear();
		layer = 0;
		capturesInput = true;
		enabled = true;
		needReload = true;
	}
};

HY_DECLARE_ECS_TYPE(UIView, "engine.uiview");

struct UIViewRuntime : public IComponent
{
	// 共享的资产数据（所有引用同一份 .ui 的实体共用）。
	std::shared_ptr<UI::ParseResult> document;

	// 这个实体自己的运行时状态（开关/滑块/焦点/输入缓冲），和文档分开存。
	std::shared_ptr<UI::UiInstanceState> instance;

	bool faulted = false;

	void OnRemove(const Entity& entity) override
	{
		document.reset();
		instance.reset();
		faulted = false;
	}

	bool ShouldDuplicate() const override { return false; }
};

HY_DECLARE_ECS_TYPE(UIViewRuntime, "hy.engine.component.UIViewRuntime");

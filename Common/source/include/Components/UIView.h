#pragma once

#include "ECSCore/IComponent.h"
#include "CommonData/AssetGUIDDef.h"
#include "UI/UiTypes.h"

#include <memory>

struct UIView : public IComponent
{
	AssetGUID documentGuid;

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

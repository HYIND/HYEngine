#pragma once

#include "ECSCore/IComponent.h"
#include "CommonData/AssetGUIDDef.h"
#include "UI/UiTypes.h"

#include <memory>

struct UIView : public IComponent
{
	AssetGUID documentGuid;
	int layer = 0;
	bool capturesInput = true;
	bool enabled = true;
	bool needReload = true;

	void OnRemove(const Entity& entity) override
	{
		documentGuid.clear();
		layer = 0;
		capturesInput = true;
		enabled = true;
		needReload = true;
	}
};

HY_DECLARE_ECS_TYPE(UIView, "engine.uiview");

struct UIViewRuntime : public IComponent
{
	std::shared_ptr<UI::ParseResult> document;
	bool faulted = false;

	void OnRemove(const Entity& entity) override
	{
		document.reset();
		faulted = false;
	}

	bool ShouldDuplicate() const override { return false; }
};

HY_DECLARE_ECS_TYPE(UIViewRuntime, "hy.engine.component.UIViewRuntime");

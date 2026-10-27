#pragma once

#include "ECSCore/IComponent.h"
#include "Script/ScriptSourceData.h"
#include <sol/sol.hpp>

struct Script : public IComponent
{
	std::shared_ptr<ScriptSourceData> data;
	bool enabled = true;
	bool needReload = true;

	Script() = default;
	Script(const Script& other) {
		data = other.data;
		enabled = other.enabled;
		needReload = true;
	}
	Script(const std::shared_ptr<ScriptSourceData>& data) :data(data) {}
	Script(const std::string& source, const AssetGUID& scriptGuid = "")
	{
		data = std::make_shared<ScriptSourceData>();
		data->scriptGuid = scriptGuid;
		data->source = source;
	}

	void OnRemove(const Entity& entity) override
	{
		data.reset();
		enabled = true;
		needReload = true;
	}
};

HY_DECLARE_ECS_TYPE(Script, "engine.script");

struct ScriptEnv
{
	// Lua 运行时环境
	sol::environment environment;
	sol::protected_function onCreate;
	sol::protected_function onUpdate;
	sol::protected_function onFixedUpdate;
	sol::protected_function onDestroy;

	bool created = false;
	bool faulted = false;
};

struct ScriptRuntime : public IComponent
{
	// 当前成功加载的脚本配置
	AssetGUID loadedGuid;
	std::string source;

	// 最近一次失败的配置标识，用于避免每帧重复加载
	std::shared_ptr<ScriptEnv> env;

	void OnRemove(const Entity& entity) override
	{
		loadedGuid.clear();
		source.clear();
		env.reset();
	}

	bool ShouldDuplicate() const override { return false; }
};

HY_DECLARE_ECS_TYPE(ScriptRuntime, "hy.engine.component.ScriptRuntime");

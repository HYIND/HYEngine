#pragma once

#include "CommonData/AssetGUIDDef.h"
#include "ECSCore/IComponent.h"
#include <string>
#include <sol/sol.hpp>

struct Script : public IComponent
{
	AssetGUID scriptGuid;
	std::string source;
	bool enabled = true;
	bool needReload = true;
};

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
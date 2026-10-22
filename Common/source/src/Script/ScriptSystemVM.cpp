#pragma once

#include "Script/ScriptSystemVM.h"
#include "Script/BaseScriptModule.h"
#include "ECSCore/World.h"
#include <unordered_set>

ScriptSystemVM::ScriptSystemVM(World* world) :_world(world) {}

void ScriptSystemVM::Initialize()
{
	if (_initialized)
		return;

	_lua.open_libraries(
		sol::lib::base,
		sol::lib::math,
		sol::lib::string,
		sol::lib::table,
		sol::lib::utf8
	);

	_lua["dofile"] = sol::nil;
	_lua["loadfile"] = sol::nil;
	_lua["load"] = sol::nil;
	_lua["collectgarbage"] = sol::nil;

	_entityType = _lua.new_usertype<Entity>(
		"Entity", sol::no_constructor,
		"is_valid", [world = _world](Entity entity) {
			return world && world->IsValidWorldEntity(entity);
		}
	);

	RegisterBuiltinApi();
	CheckRegisterChange();

	_initialized = true;
	_needUpdateModule = false;
}

void ScriptSystemVM::CheckRegisterChange()
{
	if (!_needUpdateModule)
		return;

	for (auto& [name, entry] : _modules)
	{
		if (entry.registered)
			continue;

		entry.module->Register(_lua, _entityType);
		auto funcs = entry.module->ComponentRegister();
		for (auto& [name, func] : funcs)
			_componentFunc[name] = func;
		entry.registered = true;
	}

	_needUpdateModule = false;
}

void ScriptSystemVM::CheckScriptsChange()
{
	if (!_world) return;

	auto views = _world->getViewWith<Script>();
	for (auto [entity, script] : views)
	{
		if (!_world->IsValidWorldEntity(entity)) continue;

		if (!script.enabled)
			continue;

		bool hasComponent = entity.hasComponent<ScriptRuntime>();
		bool needReload = script.needReload || !hasComponent;
		if (needReload)
		{
			if (hasComponent)
			{
				auto& runtime = entity.getComponent<ScriptRuntime>();
				if (runtime.env)
					Stop(runtime);
			}

			auto& runtime = hasComponent ? entity.getComponent<ScriptRuntime>() : entity.addComponent<ScriptRuntime>();
			if (Reload(entity, script, runtime))
				Start(runtime);

			script.needReload = false;
		}
	}

}

void ScriptSystemVM::UpdateScripts(float deltaTime)
{
	if (!_world) return;

	auto views = _world->getViewWith<Script, ScriptRuntime>();
	for (auto [entity, script, runtime] : views)
	{
		auto& env = runtime.env;
		if (script.enabled && env && env->created && !env->faulted)
			Call(env, "OnUpdate", env->onUpdate, deltaTime);
	}
}

void ScriptSystemVM::FixedUpdateScripts(float deltaTime)
{
	if (!_world) return;

	auto views = _world->getViewWith<Script, ScriptRuntime>();
	for (auto [entity, script, runtime] : views)
	{
		auto& env = runtime.env;
		if (script.enabled && env && env->created && !env->faulted)
			Call(env, "OnFixedUpdate", env->onFixedUpdate, deltaTime);
	}
}

void ScriptSystemVM::RegisterModule(const std::string& name, std::unique_ptr<IScriptModule> module)
{
	_modules[name] = ModuleEntry{ .module = std::move(module),.registered = false };
	_needUpdateModule = true;
}

void ScriptSystemVM::RegisterBuiltinApi()
{
	if (_modules.find("Transform") == _modules.end())
		RegisterModule("Transform", std::make_unique<TransformScriptModule>(_world));
	if (_modules.find("GLMMath") == _modules.end())
		RegisterModule("GLMMath", std::make_unique<GLMMathModule>());
	if (_modules.find("Physics") == _modules.end())
		RegisterModule("Physics", std::make_unique<PhysicsScriptModule>(_world));
}

sol::protected_function ScriptSystemVM::GetCallback(sol::environment& env, const char* name)
{
	sol::object object = env[name];
	if (!object.valid() || object == sol::nil || !object.is<sol::function>()) return {};
	return object.as<sol::protected_function>();
}

bool ScriptSystemVM::Call(ScriptEnv& env, const char* callback, sol::protected_function& function, float delta)
{
	if (env.faulted || !function.valid())
		return true;

	try
	{
		sol::protected_function_result result = callback == nullptr ? function() : function(delta);
		if (!result.valid())
		{
			const sol::error error = result;
			std::cerr << "[ScriptSystem]" << " callback " << callback << " failed: " << error.what() << std::endl;
			env.faulted = true;
			return false;
		}
	}
	catch (const std::exception& error)
	{
		std::cerr << "[ScriptSystem] callback "
			<< (callback ? callback : "<unknown>")
			<< " threw exception: " << error.what() << std::endl;
		env.faulted = true;
		return false;
	}

	return true;
}

bool ScriptSystemVM::Call(const std::shared_ptr<ScriptEnv>& env, const char* callback, sol::protected_function& function, float delta)
{
	if (!env)
		return false;
	return Call(*env, callback, function, delta);
}

void ScriptSystemVM::Stop(ScriptRuntime& runtime)
{
	auto& env = runtime.env;
	if (!env)
		return;

	if (env->onDestroy.valid())
		Call(env, "OnDestroy", env->onDestroy);
	env.reset();
}

bool ScriptSystemVM::Build(std::shared_ptr<ScriptEnv>& env, const std::string& source, Entity entity, const std::string& chunkName)
{
	env->environment = sol::environment(_lua, sol::create, _lua.globals());
	env->environment["self"] = entity;

	sol::protected_function_result result = _lua.safe_script(
		source,
		env->environment,
		sol::script_pass_on_error,
		chunkName,
		sol::load_mode::text
	);

	if (!result.valid())
	{
		const sol::error error = result;
		std::cerr << "[ScriptSystem] Failed to load " << chunkName << ": " << error.what() << std::endl;
		return false;
	}

	env->onCreate = GetCallback(env->environment, "OnCreate");
	env->onUpdate = GetCallback(env->environment, "OnUpdate");
	env->onFixedUpdate = GetCallback(env->environment, "OnFixedUpdate");
	env->onDestroy = GetCallback(env->environment, "OnDestroy");

	sol::object obj = env->environment["require_components"];
	if (obj.get_type() != sol::type::table)
		return false;

	sol::table requirements = obj.as<sol::table>();
	std::unordered_set<std::string> componentNames;
	for (const auto& entry : requirements)
	{
		sol::object value = entry.second;
		if (value.get_type() != sol::type::string)
			return false;
		componentNames.insert(value.as<std::string>());
	}

	for (auto& name : componentNames)
	{
		if (auto it = _componentFunc.find(name); it != _componentFunc.end())
		{
			auto& func = it->second;
			if (func) func(*_world, entity);
		}
	}

	env->faulted = false;
	env->created = false;
}

bool ScriptSystemVM::Reload(Entity entity, const Script& script, ScriptRuntime& runtime)
{
	try
	{
		if (!script.data)
			return false;

		auto& data = script.data;
		const std::string chunkName = data->scriptGuid.empty()
			? "-entity-" + std::to_string(entity.getId())
			: "asset-" + data->scriptGuid;

		std::shared_ptr<ScriptEnv> env = std::make_shared<ScriptEnv>();
		if (!Build(env, data->source, entity, chunkName))
			return false;

		runtime.env = env;
		runtime.loadedGuid = data->scriptGuid;
		runtime.source = data->source;

		return true;
	}
	catch (const std::exception& error)
	{
		std::cerr << "[ScriptSystem] Failed to prepare script for entity " << entity.getId()
			<< ": " << error.what() << std::endl;
		return false;
	}
}

void ScriptSystemVM::Start(ScriptRuntime& runtime)
{
	auto& env = runtime.env;
	if (!env || env->created || env->faulted)
		return;
	if (Call(env, "OnCreate", env->onCreate))
		env->created = true;
}


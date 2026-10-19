#pragma once

#include "IScriptModule.h"
#include "Components/Script.h"
#include "CommonData/AssetGUIDDef.h"
#include <unordered_map>
#include <utility>
#include <functional>
#include <memory>
#include <string>
#include <sol/sol.hpp>

class ScriptSystemVM
{

public:
	using ScriptSourceResolver = std::function<bool(const AssetGUID&, std::string&)>;

public:
	ScriptSystemVM(World* world);

	void Initialize();

	void CheckRegisterChange();
	void CheckScriptsChange();
	void UpdateScripts(float deltaTime);
	void FixedUpdateScripts(float deltaTime);

	void SetSourceResolver(ScriptSourceResolver resolver);
	void RegisterModule(const std::string& name, std::unique_ptr<IScriptModule> module);

private:
	void RegisterBuiltinApi();
	bool Resolve(const Script& script, std::string& source) const;
	static sol::protected_function GetCallback(sol::environment& env, const char* name);
	bool Call(ScriptEnv& env, const char* callback, sol::protected_function& function, float delta = 0.0f);
	bool Call(const std::shared_ptr<ScriptEnv>& env, const char* callback, sol::protected_function& function, float delta = 0.0f);
	void Stop(ScriptRuntime& runtime);
	bool Build(std::shared_ptr<ScriptEnv>& env, const std::string& source, Entity entity, const std::string& chunkName);
	bool Reload(Entity entity, const Script& script, ScriptRuntime& runtime);
	void Start(ScriptRuntime& runtime);

private:
	struct ModuleEntry
	{
		std::unique_ptr<IScriptModule> module;
		bool registered = false;
	};

	sol::state _lua;

	ScriptSourceResolver _sourceResolver;
	World* _world = nullptr;
	bool _initialized = false;

	std::unordered_map<std::string, ModuleEntry> _modules;
	std::unordered_map<std::string, IScriptModule::ComponentRequestFunc> _componentFunc;
	bool _needUpdateModule = false;

	sol::usertype<Entity> _entityType;
};
#include "Systems/ScriptSystem.h"

#include "Components/Script.h"
#include "CommonComponent.h"
#include "ECSCore/World.h"

#include <sol/sol.hpp>

#include <iostream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <tuple>


struct ScriptEntityHandle
{
	World* world = nullptr;
	Entity entity;

	bool IsValid() const
	{
		return world && world->IsValidWorldEntity(entity);
	}

	std::tuple<bool, float, float, float> GetPosition() const
	{
		if (!IsValid())
			return { false, 0.0f, 0.0f, 0.0f };

		const Transform* transform = entity.tryGetComponent<Transform>();
		if (!transform)
			return { false, 0.0f, 0.0f, 0.0f };

		return {
			true,
			transform->position.x,
			transform->position.y,
			transform->position.z
		};
	}

	bool SetPosition(float x, float y, float z) const
	{
		if (!IsValid())
			return false;

		Transform* transform = entity.tryGetComponent<Transform>();
		if (!transform)
			return false;

		transform->position = { x, y, z };

		auto* physics = entity.tryGetComponent<Physics>();
		auto* runtime = entity.tryGetComponent<PhysicsRuntime>();

		if (physics && runtime)
		{
			physics->forceSyncTransform = true;
			if (runtime->body && !runtime->body->isActive())
				runtime->body->activate(true);
			if (physics->isCharacter && runtime->character && runtime->ghostObject && !runtime->ghostObject->isActive())
				runtime->ghostObject->activate(true);
		}

		return true;
	}

	bool Translate(float x, float y, float z) const
	{
		if (!IsValid())
			return false;

		Transform* transform = entity.tryGetComponent<Transform>();
		if (!transform)
			return false;

		transform->position += glm::vec3(x, y, z);

		auto* physics = entity.tryGetComponent<Physics>();
		auto* runtime = entity.tryGetComponent<PhysicsRuntime>();

		if (physics && runtime)
		{
			physics->forceSyncTransform = true;
			if (runtime->body && !runtime->body->isActive())
				runtime->body->activate(true);
			if (physics->isCharacter && runtime->character && runtime->ghostObject && !runtime->ghostObject->isActive())
				runtime->ghostObject->activate(true);
		}
		return true;
	}
};

struct ScriptSystem::Impl
{
	sol::state lua;

	ScriptSourceResolver sourceResolver;
	World* world = nullptr;
	bool initialized = false;

	void Initialize()
	{
		if (initialized) return;
		lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::utf8);

		// Omit direct file, package, debug, and operating-system libraries.
		lua["dofile"] = sol::nil;
		lua["loadfile"] = sol::nil;
		lua["load"] = sol::nil;
		lua["collectgarbage"] = sol::nil;

		lua.new_usertype<ScriptEntityHandle>(
			"Entity", sol::no_constructor,
			"is_valid", &ScriptEntityHandle::IsValid,
			"get_position", &ScriptEntityHandle::GetPosition,
			"set_position", &ScriptEntityHandle::SetPosition,
			"translate", &ScriptEntityHandle::Translate
		);
		initialized = true;
	}

	bool Resolve(const Script& component, std::string& source) const
	{
		return !component.scriptGuid.empty() && sourceResolver && sourceResolver(component.scriptGuid, source);
	}

	static sol::protected_function GetCallback(sol::environment& env, const char* name)
	{
		sol::object object = env[name];
		if (!object.valid() || object == sol::nil || !object.is<sol::function>()) return {};
		return object.as<sol::protected_function>();
	}

	bool Call(ScriptEnv& env, const char* callback, sol::protected_function& function, float delta = 0.0f)
	{
		if (env.faulted || !function.valid())
			return true;

		sol::protected_function_result result = callback == nullptr ? function() : function(delta);
		if (!result.valid())
		{
			const sol::error error = result;
			std::cerr << "[ScriptSystem]" << " callback " << callback << " failed: " << error.what() << std::endl;
			env.faulted = true;
			return false;
		}

		return true;
	}

	bool Call(const std::shared_ptr<ScriptEnv>& env, const char* callback, sol::protected_function& function, float delta = 0.0f)
	{
		if (!env)
			return false;
		return Call(*env, callback, function, delta);
	}

	void Stop(ScriptRuntime& runtime)
	{
		auto& env = runtime.env;
		if (env && env->onDestroy.valid())
			Call(env, "OnDestroy", env->onDestroy);
	}

	bool Build(std::shared_ptr<ScriptEnv>& env, const std::string& source, Entity entity, const std::string& chunkName)
	{
		env->environment = sol::environment(lua, sol::create, lua.globals());
		env->environment["entity"] = ScriptEntityHandle{ world, entity };

		sol::protected_function_result result = lua.safe_script(
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

		env->faulted = false;
		env->created = false;
	}

	bool Reload(Entity entity, const Script& script, ScriptRuntime& runtime)
	{
		try
		{
			const std::string chunkName = script.scriptGuid.empty()
				? "inline-entity-" + std::to_string(entity.getId())
				: "asset-" + script.scriptGuid;

			std::shared_ptr<ScriptEnv> env = std::make_shared<ScriptEnv>();
			if (!Build(env, script.source, entity, chunkName))
				return false;

			runtime.env = env;
			runtime.loadedGuid = script.scriptGuid;
			runtime.source = script.source;

			return true;
		}
		catch (const std::exception& error)
		{
			std::cerr << "[ScriptSystem] Failed to prepare script for entity " << entity.getId()
				<< ": " << error.what() << std::endl;
			return false;
		}
	}

	void Start(ScriptRuntime& runtime)
	{
		auto& env = runtime.env;
		if (!env || env->created || env->faulted)
			return;
		if (Call(env, "OnCreate", env->onCreate))
			env->created = true;
	}

	bool ReloadRequested(const AssetGUID& guid, const std::unordered_set<AssetGUID>& requests) const
	{
		return !guid.empty() && requests.find(guid) != requests.end();
	}

	void CheckChangeScripts()
	{
		if (!world) return;

		auto views = world->getViewWith<Script>();
		for (auto [entity, script] : views)
		{
			if (!world->IsValidWorldEntity(entity)) continue;

			bool hasComponent = entity.hasComponent<ScriptRuntime>();
			bool needClose = !script.enabled;

			if (needClose)
			{
				if (hasComponent)
				{
					auto& runtime = entity.getComponent<ScriptRuntime>();
					if (runtime.env)
						Stop(runtime);
				}
				continue;
			}

			bool needReload = script.needReload || !hasComponent;
			if (needReload)
			{

				auto& runtime = hasComponent ? entity.getComponent<ScriptRuntime>() : entity.addComponent<ScriptRuntime>();

				if (!script.scriptGuid.empty())
				{
					std::string source;
					if (!Resolve(script, source))
					{
						std::cerr << "[ScriptSystem] No source available for entity " << entity.getId() << std::endl;
						script.needReload = false;
						continue;
					}
					script.source = source;
				}

				if (!Reload(entity, script, runtime))
				{
					script.needReload = false;
					continue;
				}

				Start(runtime);
				script.needReload = false;
			}
		}

	}

	void UpdateScripts(float deltaTime)
	{
		if (!world) return;

		auto views = world->getViewWith<Script, ScriptRuntime>();
		for (auto [entity, script, runtime] : views)
		{
			auto& env = runtime.env;
			if (script.enabled && env && env->created && !env->faulted)
				Call(env, "OnUpdate", env->onUpdate, deltaTime);
		}
	}

	void FixedUpdateScripts(float deltaTime)
	{
		if (!world) return;

		auto views = world->getViewWith<Script, ScriptRuntime>();
		for (auto [entity, script, runtime] : views)
		{
			auto& env = runtime.env;
			if (script.enabled && env && env->created && !env->faulted)
				Call(env, "OnFixedUpdate", env->onFixedUpdate, deltaTime);
		}
	}
};

ScriptSystem::ScriptSystem()
	: System("ScriptSystem")
{}

ScriptSystem::~ScriptSystem()
{}

void ScriptSystem::onAttach(World& world)
{
	_impl = std::make_unique<Impl>();
	_impl->world = &world;
	_impl->Initialize();
}

void ScriptSystem::onDetach()
{
	if (!_impl)
		return;
	_impl.reset();
}

void ScriptSystem::update(float deltaTime)
{
	CheckNewOrReloadScript();
	ProcessUpdateScripts(deltaTime);
}

void ScriptSystem::fixedUpdate(float fixedDeltaTime)
{
	ProcessFixedUpdateScripts(fixedDeltaTime);
}

void ScriptSystem::SetSourceResolver(ScriptSourceResolver resolver)
{
	if (_impl)
		_impl->sourceResolver = std::move(resolver);
}

void ScriptSystem::CheckNewOrReloadScript()
{
	if (_impl)
		_impl->CheckChangeScripts();
}

void ScriptSystem::ProcessUpdateScripts(float deltaTime)
{
	if (_impl)
		_impl->UpdateScripts(deltaTime / 1000.0f);
}

void ScriptSystem::ProcessFixedUpdateScripts(float fixedDeltaTime)
{
	if (_impl)
		_impl->FixedUpdateScripts(fixedDeltaTime / 1000.0f);
}


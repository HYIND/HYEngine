#include "Systems/ScriptSystem.h"

#include "Components/Script.h"
#include "Components/Transform.h"
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
	std::weak_ptr<bool> lifetime;

	bool IsValid() const
	{
		const auto token = lifetime.lock();
		return token && *token && world && world->IsValidWorldEntity(entity);
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
		if (!IsValid()) return false;
		Transform* transform = entity.tryGetComponent<Transform>();
		if (!transform) return false;
		transform->position = { x, y, z };
		return true;
	}

	bool Translate(float x, float y, float z) const
	{
		if (!IsValid()) return false;
		Transform* transform = entity.tryGetComponent<Transform>();
		if (!transform) return false;
		transform->position += glm::vec3(x, y, z);
		return true;
	}
};

std::string MakeFailureKey(const ScriptComponent& component)
{
	if (!component.inlineSource.empty()) return "inline:" + component.inlineSource;
	return "asset:" + component.scriptGuid;
}

struct ScriptSystem::Impl
{
	struct Instance
	{
		AssetGUID guid;
		std::string sourceOverride;
		std::string source;
		sol::environment environment;
		sol::protected_function onCreate;
		sol::protected_function onUpdate;
		sol::protected_function onFixedUpdate;
		sol::protected_function onDestroy;
		std::shared_ptr<bool> lifetime = std::make_shared<bool>(true);
		bool created = false;
		bool faulted = false;
	};

	// Declare Lua before instances so environments and functions are destroyed first.
	sol::state lua;
	std::unordered_map<EntityID, Instance> instances;
	std::unordered_map<EntityID, std::string> failedSources;
	ScriptSourceResolver sourceResolver;
	World* world = nullptr;
	bool initialized = false;
	std::mutex reloadMutex;
	std::unordered_set<AssetGUID> reloadRequests;

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

	bool Resolve(const ScriptComponent& component, std::string& source) const
	{
		if (!component.inlineSource.empty())
		{
			source = component.inlineSource;
			return true;
		}
		return !component.scriptGuid.empty() && sourceResolver && sourceResolver(component.scriptGuid, source);
	}

	static sol::protected_function GetCallback(sol::environment& env, const char* name)
	{
		sol::object object = env[name];
		if (!object.valid() || object == sol::nil || !object.is<sol::function>()) return {};
		return object.as<sol::protected_function>();
	}

	bool Call(EntityID id, Instance& instance, const char* callback, sol::protected_function& function, float delta = 0.0f)
	{
		if (instance.faulted || !function.valid()) return true;
		sol::protected_function_result result = callback == nullptr ? function() : function(delta);
		if (result.valid()) return true;
		const sol::error error = result;
		std::cerr << "[ScriptSystem] Entity " << id << " callback " << callback
			<< " failed: " << error.what() << std::endl;
		instance.faulted = true;
		return false;
	}

	void Stop(EntityID id, Instance& instance)
	{
		if (instance.created && !instance.faulted && instance.onDestroy.valid())
			Call(id, instance, "OnDestroy", instance.onDestroy);
		if (instance.lifetime) *instance.lifetime = false;
		instance.created = false;
	}

	bool Build(Entity entity, const ScriptComponent& component, const std::string& source, Instance& output)
	{
		try
		{
			output.guid = component.scriptGuid;
			output.sourceOverride = component.inlineSource;
			output.source = source;
			output.environment = sol::environment(lua, sol::create, lua.globals());
			output.environment["entity"] = ScriptEntityHandle{ world, entity, output.lifetime };

			const std::string chunkName = component.scriptGuid.empty()
				? "inline-entity-" + std::to_string(entity.getId())
				: "asset-" + component.scriptGuid;
			sol::protected_function_result result = lua.safe_script(source, output.environment,
				sol::script_pass_on_error, chunkName, sol::load_mode::text);
			if (!result.valid())
			{
				const sol::error error = result;
				std::cerr << "[ScriptSystem] Failed to load " << chunkName << ": " << error.what() << std::endl;
				return false;
			}

			output.onCreate = GetCallback(output.environment, "OnCreate");
			output.onUpdate = GetCallback(output.environment, "OnUpdate");
			output.onFixedUpdate = GetCallback(output.environment, "OnFixedUpdate");
			output.onDestroy = GetCallback(output.environment, "OnDestroy");
			return true;
		}
		catch (const std::exception& error)
		{
			std::cerr << "[ScriptSystem] Failed to prepare script for entity " << entity.getId()
				<< ": " << error.what() << std::endl;
			return false;
		}
	}

	void Start(EntityID id, Instance& instance)
	{
		if (instance.created || instance.faulted) return;
		instance.created = true;
		Call(id, instance, "OnCreate", instance.onCreate);
	}

	bool ReloadRequested(const AssetGUID& guid, const std::unordered_set<AssetGUID>& requests) const
	{
		return !guid.empty() && requests.find(guid) != requests.end();
	}

	void UpdateScripts(float deltaTime)
	{
		if (!world) return;
		std::unordered_set<AssetGUID> requests;
		{
			std::lock_guard<std::mutex> lock(reloadMutex);
			requests.swap(reloadRequests);
		}

		std::unordered_set<EntityID> active;
		const std::vector<Entity> entities = world->getEntitiesWith<ScriptComponent>();
		for (const Entity& entity : entities)
		{
			if (!world->IsValidWorldEntity(entity)) continue;
			const EntityID id = entity.getId();
			active.insert(id);
			const ScriptComponent* component = entity.tryGetComponent<ScriptComponent>();
			if (!component || !component->enabled)
			{
				auto existing = instances.find(id);
				if (existing != instances.end())
				{
					Stop(id, existing->second);
					instances.erase(existing);
				}
				failedSources.erase(id);
				continue;
			}

			auto current = instances.find(id);
			const bool requested = ReloadRequested(component->scriptGuid, requests)
				|| (current != instances.end() && ReloadRequested(current->second.guid, requests));
			const bool changed = current == instances.end()
				|| current->second.guid != component->scriptGuid
				|| current->second.sourceOverride != component->inlineSource;
			const std::string failureKey = MakeFailureKey(*component);
			const auto failed = failedSources.find(id);
			const bool failedUnchanged = failed != failedSources.end() && failed->second == failureKey;

			if (changed || requested)
			{
				if (!(failedUnchanged && !requested))
				{
					std::string source;
					if (!Resolve(*component, source))
					{
						if (!failedUnchanged)
							std::cerr << "[ScriptSystem] No source available for entity " << id << std::endl;
						failedSources[id] = failureKey;
					}
					else
					{
						Instance replacement;
						if (Build(entity, *component, source, replacement))
						{
							if (current != instances.end())
							{
								Stop(id, current->second);
								current->second = std::move(replacement);
							}
							else
								current = instances.emplace(id, std::move(replacement)).first;
							failedSources.erase(id);
							Start(id, current->second);
						}
						else
							failedSources[id] = failureKey;
					}
				}
			}

			current = instances.find(id);
			if (current != instances.end())
				Call(id, current->second, "OnUpdate", current->second.onUpdate, deltaTime);
		}

		for (auto it = instances.begin(); it != instances.end();)
		{
			if (active.find(it->first) == active.end())
			{
				Stop(it->first, it->second);
				it = instances.erase(it);
			}
			else ++it;
		}
		for (auto it = failedSources.begin(); it != failedSources.end();)
		{
			if (active.find(it->first) == active.end()) it = failedSources.erase(it);
			else ++it;
		}
	}

	void FixedUpdateScripts(float deltaTime)
	{
		for (auto& pair : instances)
			Call(pair.first, pair.second, "OnFixedUpdate", pair.second.onFixedUpdate, deltaTime);
	}
};

ScriptSystem::ScriptSystem() : System("ScriptSystem"), _impl(std::make_unique<Impl>())
{}

ScriptSystem::~ScriptSystem()
{
	onDetach();
}

void ScriptSystem::onAttach(World& world)
{
	_impl->world = &world;
	_impl->Initialize();
}

void ScriptSystem::onDetach()
{
	if (!_impl) return;
	for (auto& pair : _impl->instances)
		_impl->Stop(pair.first, pair.second);
	_impl->instances.clear();
	_impl->failedSources.clear();
	_impl->world = nullptr;
}

void ScriptSystem::update(float deltaTime)
{
	_impl->UpdateScripts(deltaTime / 1000.0f);
}

void ScriptSystem::fixedUpdate(float fixedDeltaTime)
{
	_impl->FixedUpdateScripts(fixedDeltaTime / 1000.0f);
}

void ScriptSystem::SetSourceResolver(ScriptSourceResolver resolver)
{
	_impl->sourceResolver = std::move(resolver);
}

void ScriptSystem::RequestReload(const AssetGUID& scriptGuid)
{
	if (scriptGuid.empty()) return;
	std::lock_guard<std::mutex> lock(_impl->reloadMutex);
	_impl->reloadRequests.insert(scriptGuid);
}

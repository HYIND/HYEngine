#include "Systems/ScriptSystem.h"

#include "Components/Script.h"
#include "CommonComponent.h"
#include "ECSCore/World.h"


ScriptSystem::ScriptSystem() : System("ScriptSystem")
{}

ScriptSystem::~ScriptSystem()
{}

void ScriptSystem::onAttach(World& world)
{
	_impl = std::make_unique<ScriptSystemVM>(&world);
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
	if (!_impl)
		return;

	_impl->CheckRegisterChange();
	_impl->CheckScriptsChange();
	_impl->UpdateScripts(deltaTime / 1000.0f);
}

void ScriptSystem::fixedUpdate(float fixedDeltaTime)
{
	if (!_impl)
		return;

	_impl->FixedUpdateScripts(fixedDeltaTime / 1000.0f);
}

bool ScriptSystem::RegisterScriptModule(const std::string& name, std::unique_ptr<IScriptModule> module)
{
	if (!module || !_impl)
		return false;

	_impl->RegisterModule(name, std::move(module));
	return true;
}

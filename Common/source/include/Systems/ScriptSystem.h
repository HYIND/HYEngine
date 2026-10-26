#pragma once

#include "Script/ScriptSystemVM.h"
#include "ECSCore/System.h"

class ScriptSystem : public System
{
public:
	ScriptSystem();
	~ScriptSystem() override;

	void onAttach(World& world) override;
	void onDetach() override;
	void update(float deltaTime) override;
	void fixedUpdate(float fixedDeltaTime) override;

public:
	bool RegisterScriptModule(const std::string& name, std::unique_ptr<IScriptModule> module);

private:
	std::unique_ptr<ScriptSystemVM> _impl;
};

HY_DECLARE_ECS_TYPE(ScriptSystem, "hy.engine.system.Script");

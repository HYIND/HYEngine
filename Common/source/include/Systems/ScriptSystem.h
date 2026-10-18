#pragma once

#include "ECSCore/System.h"
#include "CommonData/AssetGUIDDef.h"
#include <functional>
#include <memory>
#include <string>

class World;

class ScriptSystem : public System
{
public:
	using ScriptSourceResolver = std::function<bool(const AssetGUID&, std::string&)>;

	ScriptSystem();
	~ScriptSystem() override;

	void onAttach(World& world) override;
	void onDetach() override;
	void update(float deltaTime) override;
	void fixedUpdate(float fixedDeltaTime) override;

	void SetSourceResolver(ScriptSourceResolver resolver);

private:
	void CheckNewOrReloadScript();
	void ProcessUpdateScripts(float deltaTime);
	void ProcessFixedUpdateScripts(float deltaTime);

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;
};

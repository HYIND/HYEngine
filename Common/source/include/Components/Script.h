#pragma once

#include "CommonData/AssetGUIDDef.h"
#include "ECSCore/IComponent.h"
#include <string>

struct ScriptComponent : public IComponent
{
	AssetGUID scriptGuid;
	std::string inlineSource;
	bool enabled = true;
};

#pragma once

#include "ECSCore/IComponent.h"

struct TagLight :public IComponent {};
struct TagSkyBox :public IComponent {};

struct TagLightShowLight :public IComponent {};
struct TagLightShowEntity :public IComponent {};

struct TagPlayer :public IComponent {};  
struct TagCharacter :public IComponent {};

struct TagWeapon :public IComponent {};
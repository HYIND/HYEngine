#pragma once

#include "ECSCore/IComponent.h"

struct TagLightShowLight :public IComponent {};
struct TagLightShowEntity :public IComponent {};

struct TagPlayer :public IComponent {};  
struct TagCharacter :public IComponent {};

struct TagWeapon :public IComponent {};

HY_DECLARE_ECS_TYPE(TagLightShowLight, "gameplay.tag_light_show_light");
HY_DECLARE_ECS_TYPE(TagLightShowEntity, "gameplay.tag_light_show_entity");
HY_DECLARE_ECS_TYPE(TagPlayer, "gameplay.tag_player");
HY_DECLARE_ECS_TYPE(TagCharacter, "gameplay.tag_character");
HY_DECLARE_ECS_TYPE(TagWeapon, "gameplay.tag_weapon");

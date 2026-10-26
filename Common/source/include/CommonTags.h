#pragma once

#include "ECSCore/IComponent.h"


struct TagDestroy :public IComponent {};			//销毁标记
struct TagLifeTimeOut :public IComponent {};		//生命周期到期标记
struct TagMainCamera :public IComponent {};

struct TagCurrentControl :public IComponent {};

struct TagFreeCamera :public IComponent { float velocity = 0.8f; };


HY_DECLARE_ECS_TYPE(TagDestroy, "engine.tag_destroy");
HY_DECLARE_ECS_TYPE(TagLifeTimeOut, "engine.tag_lifetime_out");
HY_DECLARE_ECS_TYPE(TagMainCamera, "engine.tag_main_camera");
HY_DECLARE_ECS_TYPE(TagCurrentControl, "engine.tag_current_control");
HY_DECLARE_ECS_TYPE(TagFreeCamera, "engine.tag_free_camera");

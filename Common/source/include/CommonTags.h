#pragma once

#include "ECSCore/IComponent.h"


struct TagPhysiscCreate :public IComponent {};		//标记是否已创建物理关联
struct TagDestroy :public IComponent {};			//销毁标记
struct TagLifeTimeOut :public IComponent {};		//生命周期到期标记
struct TagMainCamera :public IComponent {};

struct TagCurrentControl :public IComponent {};

struct TagCamera :public IComponent {};
struct TagFreeCamera :public IComponent { float velocity = 0.8f; };

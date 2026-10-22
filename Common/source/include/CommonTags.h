#pragma once

#include "ECSCore/IComponent.h"


struct TagDestroy :public IComponent {};			//销毁标记
struct TagLifeTimeOut :public IComponent {};		//生命周期到期标记
struct TagMainCamera :public IComponent {};

struct TagCurrentControl :public IComponent {};

struct TagFreeCamera :public IComponent { float velocity = 0.8f; };

struct TagLight :public IComponent {};

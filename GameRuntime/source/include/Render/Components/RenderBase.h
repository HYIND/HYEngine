#pragma once

#include "Components/Transform.h"
#include "glm/glm.hpp"
#include "ECSCore/IComponent.h"
#include "Components/Renderable.h"

enum class RenderLayer2D :int
{
	LayerDefault = 0,
	UILayer = 10000
};
// 2D可渲染组件基类
struct RenderBase2D :public Renderable
{
	int layer = (int)RenderLayer2D::LayerDefault;		// 渲染层级
	int internalZOrder = 0;							// 层级内zorder

	glm::vec2 offset = glm::vec2(0.f, 0.f);	//渲染中心相对于Transform2D的偏移
};
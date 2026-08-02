#pragma once

#include "ECS/Components/Transform.h"
#include "glm/glm.hpp"
#include "ECS/Core/IComponent.h"


// 3D可渲染组件基类
struct RenderBase :public IComponent
{
	glm::vec3 offset{ 0,0,0 };	//渲染中心相对于Transform的偏移
};


enum class RenderLayer2D :int
{
	LayerDefault = 0,
	UILayer = 10000
};
// 2D可渲染组件基类
struct RenderBase2D :public IComponent
{
	int layer = (int)RenderLayer2D::LayerDefault;		// 渲染层级
	int internalZOrder = 0;							// 层级内zorder

	glm::vec2 offset = glm::vec2(0.f, 0.f);	//渲染中心相对于Transform2D的偏移
};
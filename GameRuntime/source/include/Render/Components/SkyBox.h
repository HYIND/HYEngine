#pragma once

#include "ECSCore/IComponent.h"
#include "OpenGLRenderEngine/Base/TextureCube.h"

struct SkyBox :public Renderable
{
	std::shared_ptr<TextureCube> cube;

	SkyBox() {}
	SkyBox(std::shared_ptr<TextureCube> cube) : cube(cube) {}
};
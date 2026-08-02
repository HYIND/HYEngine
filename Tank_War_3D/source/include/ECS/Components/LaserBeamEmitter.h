
#pragma once

#include "ECS/Core/IComponent.h"
#include "OpenGLRenderEngine/Base/LaserBeam.h"
#include "Helper/Tools.h"

struct LaserBeamEmitter :IComponent
{
	bool enable = true;
	std::shared_ptr<LaserBeamProperties> properties;

	LaserBeamEmitter(){}
	LaserBeamEmitter(const glm::vec3& color)
	{
		properties = std::make_shared<LaserBeamProperties>();
		properties->color = color;
		properties->white_width = 0.05f;
		properties->color_width = 0.05f;
	}
};

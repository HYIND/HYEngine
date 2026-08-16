#pragma once

#include <vector>
#include <variant>
#include "RenderEngine/D2DTools.h"
#include "OpenGLRenderEngine/General/OpenGLRenderContext.h"
#include "RenderEngine/D2DRenderContext.h"
#include "glm/glm.hpp"

namespace Render
{
	struct RenderFrameData
	{
		uint64_t frameId = 0;
		std::vector<std::shared_ptr<D2DRenderContext::RenderContext>> D2D_Contexts;
		std::vector<std::shared_ptr<OpenGLRenderContext::RenderContext>> GL_Contexts;

		std::shared_ptr<TextureCube> skybox;

		glm::mat4 projection;
		glm::mat4 view;
		glm::vec3 position;
		glm::vec3 direction;
		glm::vec3 directionUp;
		glm::vec3 directionRight;
		float nearPlane = 0.1f;
		float farPlane = 500.f;
		float fov = 80.f;

		void reset();
	};
}
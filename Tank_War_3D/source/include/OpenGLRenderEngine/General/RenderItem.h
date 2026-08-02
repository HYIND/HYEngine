#pragma once

#include "glm/glm.hpp"
#include "OpenGLRenderEngine/Base/Model.h"
#include "OpenGLRenderEngine/General/OpenGLRenderContext.h"

namespace OpenGLRender
{
	struct Item
	{
		MeshInfo meshinfo;
		std::vector<OpenGLRenderContext::AnimatorView> animatorViews;
		bool enable = true;
	};

	struct SceneItem :public Item
	{
		glm::mat4 transform = glm::mat4(1.0f);
		glm::mat4 lastTransform = glm::mat4(1.0f);
		bool isFpsSelfModel = false;
	};

	struct SceneTransparentItem :public SceneItem
	{
	};

	struct FirstPersonItem : public Item
	{
		glm::mat4 cameraView = glm::mat4(1.0f);
	};
}
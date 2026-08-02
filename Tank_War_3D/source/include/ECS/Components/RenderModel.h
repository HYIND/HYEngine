#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include "glm\glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/quaternion.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "ECS/Core/IComponent.h"
#include "OpenGLRenderEngine/General/OpenGLRenderContext.h"

extern class Model;

struct RenderModel : public IComponent
{
	std::shared_ptr<Model> model;
	glm::mat4 trans = glm::mat4(1.0f);

	OpenGLRenderContext::TransFormView renderView;

	RenderModel() {}
	RenderModel(std::shared_ptr<Model> model
	) : model(model) {
	}
};

struct FirstPersonRenderModel : public IComponent
{
	std::shared_ptr<Model> model;
	glm::mat4 cameraView = glm::mat4(1.0f);

	FirstPersonRenderModel() {}
	FirstPersonRenderModel(std::shared_ptr<Model> model
	) : model(model) {
	}
};
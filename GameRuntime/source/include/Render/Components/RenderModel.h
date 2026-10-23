#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include "glm\glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/quaternion.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "ECSCore/IComponent.h"
#include "Components/Renderable.h"
#include "VulkanRenderEngine/General/VKRenderContext.h"

extern class Model;

struct RenderModel : public Renderable
{
	std::shared_ptr<Model> model;
	glm::mat4 trans = glm::mat4(1.0f);

	std::shared_ptr<VKRenderContext::TransformHistory> transformHistory = std::make_shared<VKRenderContext::TransformHistory>();

	RenderModel() {}
	RenderModel(std::shared_ptr<Model> model
	) : model(model) {}

	RenderModel(const RenderModel& other)
		:Renderable(other),
		model(other.model),
		trans(other.trans),
		transformHistory(std::make_shared<VKRenderContext::TransformHistory>())
	{}
	RenderModel& operator=(const RenderModel& other)
	{
		if (this == &other)
			return *this;
		Renderable::operator=(other);

		model = other.model;
		trans = other.trans;
		transformHistory = std::make_shared<VKRenderContext::TransformHistory>();

		return *this;
	}

	RenderModel(RenderModel&& other) noexcept
		: Renderable(std::move(other)),
		model(std::move(other.model)),
		trans(std::move(other.trans)),
		transformHistory(std::make_shared<VKRenderContext::TransformHistory>())
	{}

	RenderModel& operator=(RenderModel&& other) noexcept
	{
		if (this == &other)
			return *this;

		Renderable::operator=(std::move(other));

		model = std::move(other.model);
		trans = other.trans;

		transformHistory = std::make_shared<VKRenderContext::TransformHistory>();

		return *this;
	}

	virtual void OnAdd(const Entity& e) {}
	virtual void OnRemove(const Entity& e) {
		model.reset();
	}
};

struct FirstPersonRenderModel : public Renderable
{
	std::shared_ptr<Model> model;
	glm::mat4 cameraView = glm::mat4(1.0f);

	FirstPersonRenderModel() {}
	FirstPersonRenderModel(std::shared_ptr<Model> model
	) : model(model) {}
	virtual void OnRemove(const Entity& e) {
		model.reset();
	}
};
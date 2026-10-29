#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include "glm\glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/quaternion.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "ECSCore/IComponent.h"
#include "Components/Renderable.h"
#include "VulkanRenderEngine/General/VKRenderContext.h"
#include <unordered_map>

extern class SkinnedModel;

struct RenderSkinnedModel : public Renderable
{
	std::shared_ptr<SkinnedModel> model;
	std::unordered_map<uint32_t, AssetGUID> materialOverrideGUIDs;
	std::unordered_map<uint32_t, MaterialProperties> materialOverrideProperties;
	glm::mat4 trans = glm::mat4(1.0f);

	std::shared_ptr<VKRenderContext::TransformHistory> transformHistory = std::make_shared<VKRenderContext::TransformHistory>();

	RenderSkinnedModel() {}
	RenderSkinnedModel(std::shared_ptr<SkinnedModel> model)
		: model(model) {}

	RenderSkinnedModel(const RenderSkinnedModel& other)
		:Renderable(other),
		model(other.model),
		materialOverrideGUIDs(other.materialOverrideGUIDs),
		materialOverrideProperties(other.materialOverrideProperties),
		trans(other.trans),
		transformHistory(std::make_shared<VKRenderContext::TransformHistory>())
	{}
	RenderSkinnedModel& operator=(const RenderSkinnedModel& other)
	{
		if (this == &other)
			return *this;
		Renderable::operator=(other);

		model = other.model;
		materialOverrideGUIDs = other.materialOverrideGUIDs;
		materialOverrideProperties = other.materialOverrideProperties;
		trans = other.trans;
		transformHistory = std::make_shared<VKRenderContext::TransformHistory>();

		return *this;
	}

	RenderSkinnedModel(RenderSkinnedModel&& other) noexcept
		: Renderable(std::move(other)),
		model(std::move(other.model)),
		materialOverrideGUIDs(std::move(other.materialOverrideGUIDs)),
		materialOverrideProperties(std::move(other.materialOverrideProperties)),
		trans(std::move(other.trans)),
		transformHistory(std::make_shared<VKRenderContext::TransformHistory>())
	{}

	RenderSkinnedModel& operator=(RenderSkinnedModel&& other) noexcept
	{
		if (this == &other)
			return *this;

		Renderable::operator=(std::move(other));

		model = std::move(other.model);
		materialOverrideGUIDs = std::move(other.materialOverrideGUIDs);
		materialOverrideProperties = std::move(other.materialOverrideProperties);
		trans = other.trans;
		transformHistory = std::make_shared<VKRenderContext::TransformHistory>();

		return *this;
	}

	virtual void OnAdd(const Entity& e) {}
	virtual void OnRemove(const Entity& e) {
		model.reset();
		materialOverrideGUIDs.clear();
		materialOverrideProperties.clear();
		trans = glm::mat4(1.0f);
	}
};

HY_DECLARE_ECS_TYPE(RenderSkinnedModel, "engine.render_skinned_model");

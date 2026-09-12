#pragma once

#include "vkstdafx.h"

#include "VulkanRenderEngine/Base/Light.h"
#include "VulkanRenderEngine/Base/Animator.h"
#include "VulkanRenderEngine/Base/Model.h"
#include "VulkanRenderEngine/Base/Effect.h"
#include "Helper/TripleBuffer.h"

namespace VKRenderContext
{
	struct AnimatorView
	{
		using MatDatas = std::vector<glm::mat4>;
		using MatDatasTripleBuffer = TripleBuffer<MatDatas>;

		std::shared_ptr<MatDatasTripleBuffer> matTripleBuffer = std::make_shared<MatDatasTripleBuffer>();
		std::shared_ptr<MatDatas> prevRenderMats = std::make_shared<MatDatas>();
	};

	struct TransFormView
	{
		using MatTripleBuffer = TripleBuffer<glm::mat4>;

		std::shared_ptr<MatTripleBuffer> transformTripleBuffer = std::make_shared<MatTripleBuffer>();
		std::shared_ptr<glm::mat4> prevRenderTransforms = std::make_shared<glm::mat4>();
	};

	struct BaseRenderData
	{
	};

	struct BaseModelRenderData :public BaseRenderData
	{
		std::shared_ptr<Model> model;
		std::vector<AnimatorView> animatorViews;
	};

	struct SceneModelRenderData :public BaseModelRenderData
	{
		TransFormView transformView;
	};

	struct SceneEffectRenderData :public BaseRenderData
	{
		std::shared_ptr<BaseEffectProperties> properties;
	};

	struct FirstPersonRenderData :public BaseModelRenderData
	{
		glm::mat4 cameraView;
	};

	struct BaseLightData :public BaseRenderData
	{
		bool renderCube;
	};

	struct DirLightRenderData :public BaseLightData
	{
		std::shared_ptr<DirLight> light;
	};

	struct PointLightRenderData :public BaseLightData
	{
		std::shared_ptr<PointLight> light;
	};

	struct SpotLightRenderData :public BaseLightData
	{
		std::shared_ptr<SpotLight> light;
	};

	enum class RenderContextType
	{
		Model,
		DirLight,
		PointLight,
		SpotLight,
		FirstPersonModel,
		Effect
	};

	struct RenderContext
	{
		RenderContextType type;
		std::shared_ptr<BaseRenderData> data;
	};
}
#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include "glm\glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/quaternion.hpp"

#include "OpenGLRenderEngine/Base/Light.h"
#include "OpenGLRenderEngine/Base/Animator.h"
#include "OpenGLRenderEngine/Base/Model.h"
#include "OpenGLRenderEngine/Base/Effect.h"
#include "Helper/TripleBuffer.h"

namespace OpenGLRenderContext
{
	struct AnimatorView
	{
		using MatDatas = std::vector<glm::mat4>;
		using MatDatasTripleBuffer = TripleBuffer<MatDatas>;

		std::shared_ptr<MatDatasTripleBuffer> matTripleBuffer = std::make_shared<MatDatasTripleBuffer>();
		std::shared_ptr<MatDatas> lastRenderMats = std::make_shared<MatDatas>();
	};

	struct TransFormView
	{
		using MatTripleBuffer = TripleBuffer<glm::mat4>;

		std::shared_ptr<MatTripleBuffer> transformTripleBuffer = std::make_shared<MatTripleBuffer>();
		std::shared_ptr<glm::mat4> lastRenderTransforms = std::make_shared<glm::mat4>();
	};

	struct BaseRenderData
	{
	};

	struct ModelRenderData :public BaseRenderData
	{
		std::shared_ptr<Model> model;
		TransFormView transformView;
		std::vector<AnimatorView> animatorViews;
	};

	struct SceneModelRenderData :public ModelRenderData
	{
		bool isFpsSelfModel = false;
	};

	struct FirstPersonRenderData :public BaseRenderData
	{
		std::shared_ptr<Model> model;
		std::vector<AnimatorView> animatorViews;
		glm::mat4 cameraView;
	};

	struct DirLightRenderData :public BaseRenderData
	{
		std::shared_ptr<DirLight> light;
		bool renderCube = true;
	};

	struct PointLightRenderData :public BaseRenderData
	{
		std::shared_ptr<PointLight> light;
		bool renderCube = true;
	};

	struct SpotLightRenderData :public BaseRenderData
	{
		std::shared_ptr<SpotLight> light;
		bool renderCube = true;
	};

	struct EffectRenderData :public BaseRenderData
	{
		std::shared_ptr<BaseEffectProperties> properties;
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
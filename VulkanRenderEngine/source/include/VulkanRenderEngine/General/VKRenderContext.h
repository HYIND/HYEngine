#pragma once

#include "vkstdafx.h"

#include "VulkanRenderEngine/Base/Light.h"
#include "VulkanRenderEngine/Base/Animator.h"
#include "VulkanRenderEngine/Base/StaticModel.h"
#include "VulkanRenderEngine/Base/SkinnedModel.h"
#include "VulkanRenderEngine/Base/Effect.h"
#include "Helper/TripleBuffer.h"

namespace VKRenderContext
{

	struct TransformHistory
	{
		glm::mat4 prevTransform{ 1.0f };
		bool valid = false;
		uint32_t committedFrame = 0xFFFFFFFFu;
	};

	struct BaseRenderData
	{
	};

	struct ModelRenderData :public BaseRenderData
	{
		glm::mat4 transform;
		std::shared_ptr<TransformHistory> transformHistory;
	};

	struct BaseLightData :public BaseRenderData
	{
		bool renderCube;
	};

	struct EffectRenderData :public BaseRenderData
	{
		std::shared_ptr<BaseEffectProperties> properties;
	};

	struct StaticModelRenderData :public ModelRenderData
	{
		std::shared_ptr<StaticModel> model;
	};

	struct BonePalette
	{
		std::vector<glm::mat4> matrices;
	};

	struct SkinnedModelRenderData :public ModelRenderData
	{
		std::shared_ptr<SkinnedModel> model;
		// 每帧在 RenderSystem 里新建一份，之后整条链共享同一个指针
		std::shared_ptr<BonePalette> palette;
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
		StaticModel,
		SkinnedModel,
		DirLight,
		PointLight,
		SpotLight,
		Effect
	};

	struct RenderContext
	{
		RenderContextType type;
		std::shared_ptr<BaseRenderData> data;
	};
}
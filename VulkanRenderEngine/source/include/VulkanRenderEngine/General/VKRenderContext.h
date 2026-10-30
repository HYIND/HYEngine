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

	struct BoneMatrices
	{
		glm::mat4 matrices;
		glm::mat4 normalMatrices;
	};

	struct BonePalette
	{
		std::vector<BoneMatrices> matrices;
	};

	// 蒙皮重投影的身份标记：不存任何数据，只用来在相邻两帧之间认出「同一个实体」。
	// 帧与帧之间的差异（骨架节点数、顶点区间）都存在 FrameHistoryData 里，按这个指针索引。
	struct SkinnedPoseHistory
	{
	};

	struct SkinnedModelRenderData :public ModelRenderData
	{
		std::shared_ptr<SkinnedModel> model;
		std::vector<std::shared_ptr<BonePalette>> palettes;
		std::shared_ptr<SkinnedPoseHistory> poseHistory;
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
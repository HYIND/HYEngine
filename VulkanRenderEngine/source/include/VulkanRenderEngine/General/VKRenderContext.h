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

	// 物体变换历史：只有"帧真正被提交渲染"时才由渲染线程写一次。
	// 同一物体的多个 item（不透明/透明、多个 mesh）共享这一份，
	// 所以用 committedFrame 保证一帧只推进一次。
	struct TransformHistory
	{
		glm::mat4 prevTransform{ 1.0f };
		bool valid = false;
		uint32_t committedFrame = 0xFFFFFFFFu;
	};

	struct BaseRenderData
	{
	};

	struct BaseModelRenderData :public BaseRenderData
	{
		glm::mat4 transform;
		std::shared_ptr<Model> model;
		std::vector<AnimatorView> animatorViews;
	};

	struct SceneModelRenderData :public BaseModelRenderData
	{
		std::shared_ptr<TransformHistory> transformHistory;
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
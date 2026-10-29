#pragma once

#include "glm/glm.hpp"
#include "VulkanRenderEngine/Base/StaticModel.h"
#include "VulkanRenderEngine/Base/SkinnedModel.h"
#include "VulkanRenderEngine/General/VKRenderContext.h"

namespace VKRenderObjectData
{
	struct RenderIndex {
		std::vector<size_t> oneSideIndex;
		std::vector<size_t> twoSideIndex;
	};

	struct Item
	{
		glm::mat4 transform;
		glm::mat4 prevTransform;
		std::shared_ptr<VKRenderContext::TransformHistory> transformHistory;
	};

	struct SceneStaticRenderData
	{
		struct MeshItem :public Item
		{
			StaticMeshInfo meshinfo;
		};

		std::vector<MeshItem> opaqueItems;
		std::vector<MeshItem> transparentItems;

		RenderIndex opaqueItems_renderIndex;
		RenderIndex opaqueItems_cullRenderIndex;

		std::vector<std::shared_ptr<BaseEffectProperties>> effectItems;
	};

	struct SceneSkinnedRenderData
	{
		struct SkinnedMeshItem :public Item
		{
			SkinnedMeshInfo meshinfo;
			std::shared_ptr<VKRenderContext::BonePalette> palette;
		};

		std::vector<SkinnedMeshItem> opaqueOneSideItems;
		std::vector<SkinnedMeshItem> opaqueTwoSideItems;
		std::vector<SkinnedMeshItem> transparentItems;
	};
}
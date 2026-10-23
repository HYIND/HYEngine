#pragma once

#include "glm/glm.hpp"
#include "VulkanRenderEngine/Base/Model.h"
#include "VulkanRenderEngine/General/VKRenderContext.h"

namespace VKRenderObjectData
{
	struct RenderIndex {
		std::vector<size_t> oneSideIndex;
		std::vector<size_t> twoSideIndex;
	};

	struct SceneRenderData
	{
		struct Item
		{
			glm::mat4 transform;
			glm::mat4 prevTransform;
			// 所属物体的变换历史（shared_ptr 保证生命周期）。
			// 分析阶段只填默认值，真正的 prev 由渲染线程在帧提交时解析
			std::shared_ptr<VKRenderContext::TransformHistory> transformHistory;
		};
		struct MeshItem :public Item
		{
			MeshInfo meshinfo;
		};
		struct ModelItem :public Item
		{
			std::vector<MeshInfo> models;
		};

		struct OpaqueMeshItem :public MeshItem {};
		struct TransparentMeshItem :public MeshItem {};

		struct Skinned {
			std::shared_ptr<std::vector<VKRenderContext::AnimatorView>> animators;
		};
		struct TransparentSkinnedMeshItem :public Skinned, public MeshItem {};
		struct OpaqueSkinnedModelItem :public Skinned, public ModelItem {};


		std::vector<OpaqueMeshItem> opaqueMesh;
		std::vector<TransparentMeshItem> transparentMesh;
		std::vector<OpaqueSkinnedModelItem> opaqueSkinnedModel;
		std::vector<TransparentSkinnedMeshItem> transparentSkinnedMesh;

		RenderIndex opaqueMesh_renderIndex;
		RenderIndex opaqueMesh_cullRenderIndex;
		std::vector<size_t> transparentMesh_SortIndex;
		std::vector<std::vector<size_t>> opaqueSkinnedModel_SortIndex;
		std::vector<size_t> transparentSkinnedMesh_SortIndex;

		std::vector<std::shared_ptr<BaseEffectProperties>> effectItems;
	};

	struct FirstPersonRenderData
	{
		struct Item
		{
			glm::mat4 cameraView;
		};
		struct MeshItem :public Item
		{
			MeshInfo meshinfo;
		};
		struct ModelItem :public Item
		{
			std::vector<MeshInfo> models;
		};

		struct OpaqueMeshItem :public MeshItem {};
		struct TransparentMeshItem :public MeshItem {};

		struct Skinned {
			std::shared_ptr<std::vector<VKRenderContext::AnimatorView>> animators;
		};
		struct TransparentSkinnedMeshItem :public Skinned, public MeshItem {};
		struct OpaqueSkinnedModelItem :public Skinned, public ModelItem {};

		std::vector<OpaqueMeshItem> opaqueMesh;
		std::vector<TransparentMeshItem> transparentMesh;
		std::vector<OpaqueSkinnedModelItem> opaqueSkinnedModel;
		std::vector<TransparentSkinnedMeshItem> transparentSkinnedMesh;
	};
}
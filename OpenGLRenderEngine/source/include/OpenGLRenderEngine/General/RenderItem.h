#pragma once

#include "glm/glm.hpp"
#include "OpenGLRenderEngine/Base/Model.h"
#include "OpenGLRenderEngine/General/OpenGLRenderContext.h"

namespace OpenGLRenderObjectData
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
			std::shared_ptr<std::vector<OpenGLRenderContext::AnimatorView>> animators;
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
			std::shared_ptr<std::vector<OpenGLRenderContext::AnimatorView>> animators;
		};
		struct TransparentSkinnedMeshItem :public Skinned, public MeshItem {};
		struct OpaqueSkinnedModelItem :public Skinned, public ModelItem {};

		std::vector<OpaqueMeshItem> opaqueMesh;
		std::vector<TransparentMeshItem> transparentMesh;
		std::vector<OpaqueSkinnedModelItem> opaqueSkinnedModel;
		std::vector<TransparentSkinnedMeshItem> transparentSkinnedMesh;
	};
}
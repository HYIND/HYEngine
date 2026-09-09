#pragma once

#include "glm\glm.hpp"
#include "VulkanRenderEngine/Base/DynamicBlock.h"
#include "VulkanRenderEngine/Base/Light.h"
#include "VulkanRenderEngine/Base/AtlasMap.h"
#include "VulkanRenderEngine/General/RenderItem.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "VulkanRenderEngine/General/SegmentBufferManager.h"
#include "VulkanRenderEngine/General/SegmentBufferBase.h"

#include "VulkanRenderEngine/General/RenderHelp.h"
#include "VulkanRenderEngine/General/BVHBuilder.h"
#include "VulkanRenderEngine/GlobalConfig.h"

struct alignas(16) comp_Vertex {
	alignas(16) glm::vec3 position;
};

struct alignas(16) comp_Triangle {
	comp_Vertex v0;
	comp_Vertex v1;
	comp_Vertex v2;
};

struct alignas(16) comp_Vertex_Extension {
	alignas(16) glm::vec3 normal;
	alignas(16) glm::vec2 texCoords;

	alignas(16) glm::vec3 Tangent;
	alignas(16) glm::vec3 Bitangent;
	alignas(16) int m_BoneIDs[GlobalConfig::Mesh_Max_Bone_Influence] = { -1 };
	alignas(16) float m_Weights[GlobalConfig::Mesh_Max_Bone_Influence] = { 1.0f };
};

struct alignas(16) comp_Triangle_Extension {
	comp_Vertex_Extension v0;
	comp_Vertex_Extension v1;
	comp_Vertex_Extension v2;
};

struct alignas(16) comp_MeshData
{
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 invModel;

	glm::vec4 normalMatRow1;
	glm::vec4 normalMatRow2;
	glm::vec4 normalMatRow3;

	int triangleFirst;  //triangles中的首个Triangle位置
	int triangleCount;  //Triangle数量

	int bvhNodeFirst;   //meshBVHNodeBuffer.nodes中的首个bvhnode位置
	int bvhNodeCount;   //bvhnode数量

	//int bvhIndicesFirst;   //meshBVHIndicesBuffer.indices中的首个indices位置
	//int bvhIndicesCount;   //indices数量
};

struct alignas(16) comp_MeshMatData
{
	comp_MeshData mesh;
	MaterialData material;
};

#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/General/BVHBuilder.h"
#include "VulkanRenderEngine/GlobalConfig.h"
#include "VulkanRenderEngine/VKWrapper/VmaBuffer.h"

using BoneID = uint32_t;

struct alignas(16) Vertex 
{
	static constexpr BoneID InvalidBoneID = std::numeric_limits<BoneID>::max();

	alignas(16) glm::vec3 Position;
	alignas(16) glm::vec3 Normal;
	alignas(16) glm::vec2 TexCoords;
	alignas(16) glm::vec3 Tangent;
	alignas(16) glm::vec3 Bitangent;
	alignas(16) BoneID m_BoneIDs[GlobalConfig::Mesh_Max_Bone_Influence] = { InvalidBoneID };
	alignas(16) float m_Weights[GlobalConfig::Mesh_Max_Bone_Influence] = { 1.0f };

	static std::vector<vk::VertexInputAttributeDescription> GetVertexInputAttributeDescription(uint32_t binding = 0, uint32_t locaitonOffset = 0);
	static vk::VertexInputBindingDescription GetVertexInputBindingDescription(uint32_t binding = 0);

	void SetVertexBoneDataToDefault()
	{
		for (uint32_t i = 0; i < GlobalConfig::Mesh_Max_Bone_Influence; i++)
		{
			m_BoneIDs[i] = Vertex::InvalidBoneID;
			m_Weights[i] = 0.0f;
		}
	}

	bool SetVertexBoneData(BoneID boneID, float weight)
	{
		for (uint32_t i = 0; i < GlobalConfig::Mesh_Max_Bone_Influence; ++i)
		{
			if (m_BoneIDs[i] == Vertex::InvalidBoneID)
			{
				m_Weights[i] = weight;
				m_BoneIDs[i] = boneID;
				return true;
			}
		}
		return false;
	}

};


class Mesh
{
public:
	Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);
	~Mesh();

	void CalculateTangentData();	//计算切线和副切线

	void MakeScale(const glm::vec3& scale);
	void MakeTranslate(const glm::vec3& trans);
	void MakeRotate(float angle, const glm::vec3& axis);
	void MakeTransform(const glm::mat4& mat);

	std::shared_ptr<Mesh> Clone();

	std::shared_ptr<const BVHData> GetBVHData();
	AABB GetAABB();

	const std::vector<Vertex>& GetVertices() const;
	const std::vector<unsigned int>& GetIndices() const;

	void SetDirty();

	std::string GetUUID();

	uint32_t GetVerticesIndicesVsrsion();
	uint32_t GetBVHDataVsrsion();
	uint32_t GetAABBDataVsrsion();

public:
	void SetNeedUpdateIndirectDraw(bool value);
	bool GetNeedUpdateIndirectDraw() const;

private:
	std::vector<Vertex> _vertices;
	std::vector<unsigned int> _indices;

	std::shared_ptr<BVHData> _bvhdata;
	AABB _aabbdata;

	bool needUpdateIndirectDraw;
	bool needUpdateBVH;
	bool needUpdateAABB;
	std::atomic<uint32_t> _viVersion;
	std::atomic<uint32_t> _bvhversion;
	std::atomic<uint32_t> _aabbversion;

	std::string _uuid;
};
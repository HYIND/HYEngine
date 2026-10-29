#pragma once

#include "vkstdafx.h"
#include "Mesh.h"
#include "VulkanRenderEngine/GlobalConfig.h"

using BoneID = uint32_t;

struct alignas(16) SkinnedData
{
	static constexpr BoneID InvalidBoneID = std::numeric_limits<BoneID>::max();

	alignas(16) BoneID m_BoneIDs[GlobalConfig::Mesh_Max_Bone_Influence] = { InvalidBoneID };
	alignas(16) float m_Weights[GlobalConfig::Mesh_Max_Bone_Influence] = { 0.0f };

	void SetVertexBoneDataToDefault()
	{
		for (uint32_t i = 0; i < GlobalConfig::Mesh_Max_Bone_Influence; i++)
		{
			m_BoneIDs[i] = SkinnedData::InvalidBoneID;
			m_Weights[i] = 0.0f;
		}
	}
	bool SetVertexBoneData(BoneID boneID, float weight)
	{
		for (uint32_t i = 0; i < GlobalConfig::Mesh_Max_Bone_Influence; ++i)
		{
			if (m_BoneIDs[i] == SkinnedData::InvalidBoneID)
			{
				m_Weights[i] = weight;
				m_BoneIDs[i] = boneID;
				return true;
			}
		}
		return false;
	}
};

class SkinnedMesh :public Mesh
{
public:
	static std::vector<vk::VertexInputAttributeDescription> GetVertexInputAttributeDescription(uint32_t baseBinding = 0, uint32_t locaitonOffset = 0);
	static vk::VertexInputBindingDescription GetVertexInputBindingDescription(uint32_t baseBinding = 0);

public:
	SkinnedMesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices, const std::vector<SkinnedData>& skinned);
	~SkinnedMesh();

	std::shared_ptr<SkinnedMesh> Clone();
	const std::vector<SkinnedData>& GetSkinnedData() const;

private:
	std::vector<SkinnedData> _skinned;
};
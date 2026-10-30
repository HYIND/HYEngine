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

	// 逆绑定矩阵必须是 per-mesh 的：aiBone::mOffsetMatrix 定义在「这个网格的绑定空间」里，
	// 已经把这个网格节点的变换烘焙进去了（导入器常给 mesh 节点挂 scale=100 或一个平移），
	// 所以同一根骨骼在不同网格里的矩阵并不相同，跨网格共用一份就会让别的网格整体缩放/平移。
	// 下标与 SkinnedData::m_BoneIDs 一致（骨架节点下标）。
	const std::vector<glm::mat4>& GetInverseBindMatrices() const;
	void SetInverseBindMatrices(std::vector<glm::mat4> matrices);

private:
	std::vector<SkinnedData> _skinned;
	std::vector<glm::mat4> _inverseBindMatrices;
};

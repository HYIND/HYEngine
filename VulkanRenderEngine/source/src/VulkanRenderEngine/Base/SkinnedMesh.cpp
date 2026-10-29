#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/SkinnedMesh.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"
#include "VulkanRenderEngine/VKContext.h"


std::vector<vk::VertexInputAttributeDescription> SkinnedMesh::GetVertexInputAttributeDescription(uint32_t baseBinding, uint32_t locaitonOffset)
{
	std::vector<vk::VertexInputAttributeDescription> attributes;

	attributes.push_back(
		vk::VertexInputAttributeDescription()
		.setLocation(locaitonOffset++)
		.setBinding(baseBinding)
		.setFormat(vk::Format::eR32G32B32Sfloat)
		.setOffset(offsetof(Vertex, Position))
	);

	attributes.push_back(
		vk::VertexInputAttributeDescription()
		.setLocation(locaitonOffset++)
		.setBinding(baseBinding)
		.setFormat(vk::Format::eR32G32B32Sfloat)
		.setOffset(offsetof(Vertex, Normal))
	);

	attributes.push_back(
		vk::VertexInputAttributeDescription()
		.setLocation(locaitonOffset++)
		.setBinding(baseBinding)
		.setFormat(vk::Format::eR32G32Sfloat)
		.setOffset(offsetof(Vertex, TexCoords))
	);

	attributes.push_back(
		vk::VertexInputAttributeDescription()
		.setLocation(locaitonOffset++)
		.setBinding(baseBinding)
		.setFormat(vk::Format::eR32G32B32Sfloat)
		.setOffset(offsetof(Vertex, Tangent))
	);

	attributes.push_back(
		vk::VertexInputAttributeDescription()
		.setLocation(locaitonOffset++)
		.setBinding(baseBinding)
		.setFormat(vk::Format::eR32G32B32Sfloat)
		.setOffset(offsetof(Vertex, Bitangent))
	);

	// 骨骼ID 前4个 - location 5
	attributes.push_back(
		vk::VertexInputAttributeDescription()
		.setLocation(locaitonOffset + 5)
		.setBinding(baseBinding + 1)
		.setFormat(vk::Format::eR32G32B32A32Sint)
		.setOffset(offsetof(SkinnedData, m_BoneIDs))
	);

	// 骨骼ID 后4个 - location 6
	attributes.push_back(
		vk::VertexInputAttributeDescription()
		.setLocation(locaitonOffset + 6)
		.setBinding(baseBinding + 1)
		.setFormat(vk::Format::eR32G32B32A32Sint)
		.setOffset(offsetof(SkinnedData, m_BoneIDs) + 4 * sizeof(BoneID))
	);

	// 骨骼权重 前4个 - location 7
	attributes.push_back(
		vk::VertexInputAttributeDescription()
		.setLocation(locaitonOffset + 7)
		.setBinding(baseBinding + 1)
		.setFormat(vk::Format::eR32G32B32A32Sfloat)
		.setOffset(offsetof(SkinnedData, m_Weights))
	);

	// 骨骼权重 后4个 - location 8
	attributes.push_back(
		vk::VertexInputAttributeDescription()
		.setLocation(locaitonOffset + 8)
		.setBinding(baseBinding + 1)
		.setFormat(vk::Format::eR32G32B32A32Sfloat)
		.setOffset(offsetof(SkinnedData, m_Weights) + 4 * sizeof(float))
	);

	return attributes;
}

vk::VertexInputBindingDescription SkinnedMesh::GetVertexInputBindingDescription(uint32_t baseBinding) {
	vk::VertexInputBindingDescription bindingDescription;
	bindingDescription.setBinding(baseBinding)
		.setStride(sizeof(Vertex))
		.setInputRate(vk::VertexInputRate::eVertex);
	return bindingDescription;
}


SkinnedMesh::SkinnedMesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices, const std::vector<SkinnedData>& skinned)
	:Mesh(vertices, indices)
{
	if (vertices.size() == skinned.size())
		_skinned = skinned;
	else
	{
		_skinned.resize(vertices.size());
		std::cerr << "error skinneddata cause size not equal verticedata\n";
	}

}

SkinnedMesh::~SkinnedMesh()
{
	IndirectDrawManager::Instance()->RetireSkinned(*this);
}

std::shared_ptr<SkinnedMesh> SkinnedMesh::Clone()
{
	auto other = std::make_shared<SkinnedMesh>(_vertices, _indices, _skinned);
	return other;
}

const std::vector<SkinnedData>& SkinnedMesh::GetSkinnedData() const
{
	return _skinned;
}

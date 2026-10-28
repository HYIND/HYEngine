#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/StaticMesh.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"
#include "VulkanRenderEngine/VKContext.h"


std::vector<vk::VertexInputAttributeDescription> StaticMesh::GetVertexInputAttributeDescription(uint32_t baseBinding, uint32_t locaitonOffset)
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

	return attributes;
}

vk::VertexInputBindingDescription StaticMesh::GetVertexInputBindingDescription(uint32_t baseBinding) {
	vk::VertexInputBindingDescription bindingDescription;
	bindingDescription.setBinding(baseBinding)
		.setStride(sizeof(Vertex))
		.setInputRate(vk::VertexInputRate::eVertex);
	return bindingDescription;
}


StaticMesh::StaticMesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices)
	:Mesh(vertices, indices),
	needUpdateIndirectDraw(false), needUpdateBVH(true), needUpdateAABB(true),
	_viVersion{ 0 }, _bvhversion{ 0 }, _aabbversion{ 0 }
{
	_uuid = Tool::GenerateSimpleUuid();
	SetDirty();
}

StaticMesh::~StaticMesh()
{
	IndirectDrawManager::Instance()->RetireStaticMesh(*this);
}

void StaticMesh::SetDirty()
{
	needUpdateIndirectDraw = true;
	needUpdateAABB = true;
	needUpdateBVH = true;
	_viVersion++;
	_bvhversion++;
	_aabbversion++;
}

std::shared_ptr<StaticMesh> StaticMesh::Clone()
{
	auto other = std::make_shared<StaticMesh>(_vertices, _indices);
	if (_bvhdata) other->_bvhdata = std::make_shared<BVHData>(*_bvhdata);
	return other;
}

std::shared_ptr<const BVHData> StaticMesh::GetBVHData()
{
	if (needUpdateBVH || !_bvhdata)
	{
		std::vector<AABB> triangleAABBData;

		for (int i = 0; i < _indices.size(); i += 3)
		{
			AABB aabb;
			aabb.extend(_vertices[_indices[i + 0]].Position);
			aabb.extend(_vertices[_indices[i + 1]].Position);
			aabb.extend(_vertices[_indices[i + 2]].Position);

			triangleAABBData.push_back(aabb);
		}

		_bvhdata = BVHBuilder::Build(triangleAABBData, GlobalConfig::Mesh_BVH_Leaf_TriCount, GlobalConfig::RayTrace_Max_Recursive_Depth);
		needUpdateBVH = false;
	}
	return _bvhdata;
}


AABB StaticMesh::GetAABB()
{
	if (needUpdateAABB)
	{
		AABB aabb;
		for (auto& v : _vertices)
			aabb.extend(v.Position);

		_aabbdata = aabb;
		needUpdateAABB = false;
	}
	return _aabbdata;
}

std::string StaticMesh::GetUUID()
{
	return _uuid;
}

uint32_t StaticMesh::GetVerticesIndicesVsrsion()
{
	return _viVersion.load();
}

uint32_t StaticMesh::GetBVHDataVsrsion()
{
	return _bvhversion.load();
}

uint32_t StaticMesh::GetAABBDataVsrsion()
{
	return _aabbversion.load();
}

void StaticMesh::SetNeedUpdateIndirectDraw(bool value)
{
	needUpdateIndirectDraw = value;
}

bool StaticMesh::GetNeedUpdateIndirectDraw() const
{
	return needUpdateIndirectDraw;
}

void StaticMesh::OnCalculateTangentData() { SetDirty(); }

void StaticMesh::OnMakeTransform() { SetDirty(); }

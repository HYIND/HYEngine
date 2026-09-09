#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/Mesh.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"


std::vector<vk::VertexInputAttributeDescription> Vertex::GetVertexInputAttributeDescription(uint32_t binding, uint32_t locaitonOffset)
{
	std::vector<vk::VertexInputAttributeDescription> attributes;
	attributes.resize(9, {});

	// 位置 - location 0
	attributes[0].setLocation(locaitonOffset + 0)
		.setBinding(0)
		.setFormat(vk::Format::eR32G32B32Sfloat)
		.setOffset(offsetof(Vertex, Position));

	// 法线 - location 1
	attributes[1].setLocation(locaitonOffset + 1)
		.setBinding(0)
		.setFormat(vk::Format::eR32G32B32Sfloat)
		.setOffset(offsetof(Vertex, Normal));

	// 纹理坐标 - location 2
	attributes[2].setLocation(locaitonOffset + 2)
		.setBinding(0)
		.setFormat(vk::Format::eR32G32Sfloat)
		.setOffset(offsetof(Vertex, TexCoords));

	// 切线 - location 3
	attributes[3].setLocation(locaitonOffset + 3)
		.setBinding(0)
		.setFormat(vk::Format::eR32G32B32Sfloat)
		.setOffset(offsetof(Vertex, Tangent));

	// 副切线 - location 4
	attributes[4].setLocation(locaitonOffset + 4)
		.setBinding(0)
		.setFormat(vk::Format::eR32G32B32Sfloat)
		.setOffset(offsetof(Vertex, Bitangent));

	// 骨骼ID 前4个 - location 5
	attributes[5].setLocation(locaitonOffset + 5)
		.setBinding(0)
		.setFormat(vk::Format::eR32G32B32A32Sint)
		.setOffset(offsetof(Vertex, m_BoneIDs));

	// 骨骼ID 后4个 - location 6
	attributes[6].setLocation(locaitonOffset + 6)
		.setBinding(0)
		.setFormat(vk::Format::eR32G32B32A32Sint)
		.setOffset(offsetof(Vertex, m_BoneIDs) + 4 * sizeof(int));

	// 骨骼权重 前4个 - location 7
	attributes[7].setLocation(locaitonOffset + 7)
		.setBinding(0)
		.setFormat(vk::Format::eR32G32B32A32Sfloat)
		.setOffset(offsetof(Vertex, m_Weights));

	// 骨骼权重 后4个 - location 8
	attributes[8].setLocation(locaitonOffset + 8)
		.setBinding(0)
		.setFormat(vk::Format::eR32G32B32A32Sfloat)
		.setOffset(offsetof(Vertex, m_Weights) + 4 * sizeof(float));

	return attributes;
}

vk::VertexInputBindingDescription Vertex::GetVertexInputBindingDescription(uint32_t binding) {
	vk::VertexInputBindingDescription bindingDescription;
	bindingDescription.setBinding(binding)
		.setStride(sizeof(Vertex))
		.setInputRate(vk::VertexInputRate::eVertex);
	return bindingDescription;
}


Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices)
	: _device(nullptr),
	needSetup(true), needUpdateIndirectDraw(false), needUpdateBVH(true), needUpdateAABB(true),
	_viVersion{ 0 }, _bvhversion{ 0 }, _aabbversion{ 0 }
{
	_uuid = Tool::GenerateSimpleUuid();

	this->_vertices = vertices;
	this->_indices = indices;

	_device = VKCONTEXT->GetDevice().get();


	SetDirty();
}

Mesh::~Mesh()
{
	IndirectDrawManager::Instance()->deleteMesh(*this);
}

void Mesh::Draw(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd)
{
	Need();

	std::vector<vk::Buffer> vertexbuffers = { _vertexBuffer->GetHandle() };
	std::vector<vk::DeviceSize> offsets = { 0 };

	cmd->bindVertexBuffers(0, vertexbuffers, offsets);
	cmd->bindIndexBuffer(_indexBuffer->GetHandle(), 0, vk::IndexType::eUint32);
	cmd->drawIndexed(static_cast<uint32_t>(_indices.size()), 1, 0, 0, 0);
}

void Mesh::DrawInstanced(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd, uint32_t count)
{
	Need();

	std::vector<vk::Buffer> vertexbuffers = { _vertexBuffer->GetHandle() };
	std::vector<vk::DeviceSize> offsets = { 0 };

	cmd->bindVertexBuffers(0, vertexbuffers, offsets);
	cmd->bindIndexBuffer(_indexBuffer->GetHandle(), 0, vk::IndexType::eUint32);
	cmd->drawIndexed(static_cast<uint32_t>(_indices.size()), count, 0, 0, 0);
}

void Mesh::SetDirty()
{
	needSetup = true;
	needUpdateIndirectDraw = true;
	needUpdateAABB = true;
	needUpdateBVH = true;
	_viVersion++;
	_bvhversion++;
	_aabbversion++;
}

void Mesh::Need()
{
	if (needSetup)
		SetupMesh();
}

void Mesh::CalculateTangentData()
{
	// 为每个顶点初始化累加器
	std::vector<glm::vec3> tangents(_vertices.size(), glm::vec3(0.0f));
	std::vector<glm::vec3> bitangents(_vertices.size(), glm::vec3(0.0f));

	// 遍历每个三角形
	for (size_t i = 0; i < _indices.size(); i += 3) {
		uint32_t i0 = _indices[i];
		uint32_t i1 = _indices[i + 1];
		uint32_t i2 = _indices[i + 2];

		// 获取三个顶点
		const Vertex& v0 = _vertices[i0];
		const Vertex& v1 = _vertices[i1];
		const Vertex& v2 = _vertices[i2];

		// 计算边向量
		glm::vec3 E1 = v1.Position - v0.Position;
		glm::vec3 E2 = v2.Position - v0.Position;

		// 计算UV差
		float dU1 = v1.TexCoords.x - v0.TexCoords.x;
		float dV1 = v1.TexCoords.y - v0.TexCoords.y;
		float dU2 = v2.TexCoords.x - v0.TexCoords.x;
		float dV2 = v2.TexCoords.y - v0.TexCoords.y;

		// 计算行列式
		float r = 1.0f / (dU1 * dV2 - dU2 * dV1);

		// 计算该三角形的切线空间基
		glm::vec3 T = glm::normalize(glm::vec3(
			(dV2 * E1.x - dV1 * E2.x) * r,
			(dV2 * E1.y - dV1 * E2.y) * r,
			(dV2 * E1.z - dV1 * E2.z) * r
		));

		glm::vec3 B = glm::normalize(glm::vec3(
			(-dU2 * E1.x + dU1 * E2.x) * r,
			(-dU2 * E1.y + dU1 * E2.y) * r,
			(-dU2 * E1.z + dU1 * E2.z) * r
		));

		// 累加到三个顶点的切线和副切线
		tangents[i0] += T;
		tangents[i1] += T;
		tangents[i2] += T;

		bitangents[i0] += B;
		bitangents[i1] += B;
		bitangents[i2] += B;
	}

	// 对所有顶点进行平均并正交化
	for (size_t i = 0; i < _vertices.size(); i++) {
		glm::vec3& T = tangents[i];
		glm::vec3& B = bitangents[i];
		glm::vec3& N = _vertices[i].Normal;

		// Gram-Schmidt 正交化：让T垂直于N
		T = glm::normalize(T - N * glm::dot(N, T));

		// 计算副切线：B = N × T（使用右手坐标系）
		// 注意：有些引擎用 B = cross(N, T)，取决于UV方向
		B = glm::normalize(glm::cross(N, T));

		// 可选：计算手性（handedness），用于法线贴图翻转
		// float handedness = (glm::dot(glm::cross(N, T), B) < 0.0f) ? -1.0f : 1.0f;

		_vertices[i].Tangent = T;
		_vertices[i].Bitangent = B;
	}

	SetDirty();
}

void Mesh::MakeScale(const glm::vec3& scale)
{
	if (scale.x == 1.0f && scale.y == 1.0f && scale.z == 1.0f)
		return;
	if (scale.x == scale.y && scale.y == scale.z)
	{
		for (auto& vertex : _vertices)
			vertex.Position *= scale;

		SetDirty();
		return;
	}

	glm::mat4 mat = glm::scale(glm::mat4(1.0f), scale);
	MakeTransform(mat);
}

void Mesh::MakeTranslate(const glm::vec3& trans)
{
	if (trans.x == 0.0f && trans.y == 0.0f && trans.z == 0.0f)
		return;
	for (auto& vertex : _vertices)
		vertex.Position += trans;

	SetDirty();
}

void Mesh::MakeRotate(float angle, const glm::vec3& axis)
{
	if (angle == 0.0f)
		return;
	glm::mat4 mat = glm::rotate(glm::mat4(1.0f), glm::radians(angle), axis);
	MakeTransform(mat);
}

void Mesh::MakeTransform(const glm::mat4& mat)
{
	if (mat == glm::mat4(1.0f))
		return;
	glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(mat)));
	for (auto& vertex : _vertices) {
		vertex.Position = glm::vec3(mat * glm::vec4(vertex.Position, 1.0f));
		vertex.Normal = glm::normalize(normalMat * vertex.Normal);
		vertex.Tangent = glm::normalize(normalMat * vertex.Tangent);
		vertex.Bitangent = glm::normalize(glm::cross(vertex.Normal, vertex.Tangent));
	}

	SetDirty();
}

std::shared_ptr<Mesh> Mesh::Clone()
{
	auto other = std::make_shared<Mesh>(_vertices, _indices);
	if (_bvhdata) other->_bvhdata = std::make_shared<BVHData>(*_bvhdata);
	return other;
}

std::shared_ptr<const BVHData> Mesh::GetBVHData()
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


AABB Mesh::GetAABB()
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

const std::vector<Vertex>& Mesh::GetVertices() const
{
	return _vertices;
}

const std::vector<unsigned int>& Mesh::GetIndices() const
{
	return _indices;
}

std::string Mesh::GetUUID()
{
	return _uuid;
}

uint32_t Mesh::GetVerticesIndicesVsrsion()
{
	return _viVersion.load();
}

uint32_t Mesh::GetBVHDataVsrsion()
{
	return _bvhversion.load();
}

uint32_t Mesh::GetAABBDataVsrsion()
{
	return _aabbversion.load();
}

void Mesh::SetNeedUpdateIndirectDraw(bool value)
{
	needUpdateIndirectDraw = value;
}

bool Mesh::GetNeedUpdateIndricetDraw() const
{
	return needUpdateIndirectDraw;
}

void Mesh::SetupMesh()
{
	// 计算缓冲区大小
	VkDeviceSize vertexSize = _vertices.size() * sizeof(Vertex);
	VkDeviceSize indexSize = _indices.size() * sizeof(unsigned int);

	VkBufferCreateInfo vertexBufferInfo = {};
	vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexBufferInfo.size = vertexSize;
	vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
		VK_BUFFER_USAGE_TRANSFER_DST_BIT |  // 支持更新
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; // 如果用于 Indirect 或 Compute

	VmaAllocationCreateInfo vertexAllocInfo = {};
	vertexAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;  // 让 VMA 自动选择最优内存
	vertexAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
		VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;

	_vertexBuffer = std::make_unique<VKWrapper::VmaBuffer>(
		_device, vertexBufferInfo, vertexAllocInfo
	);

	VkBufferCreateInfo indexBufferInfo = {};
	indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	indexBufferInfo.size = indexSize;
	indexBufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
		VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	VmaAllocationCreateInfo indexAllocInfo = {};
	indexAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	indexAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
		VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;

	_indexBuffer = std::make_unique<VKWrapper::VmaBuffer>(
		_device, indexBufferInfo, indexAllocInfo
	);

	UpdateGPUData();

	needSetup = false;
}

void Mesh::UpdateGPUData()
{
	if (!_vertexBuffer || !_indexBuffer)
		return;

	VkDeviceSize vertexSize = _vertices.size() * sizeof(Vertex);
	VkDeviceSize indexSize = _indices.size() * sizeof(unsigned int);

	_vertexBuffer->Update(_vertices.data(), vertexSize, 0);
	_indexBuffer->Update(_indices.data(), indexSize, 0);
}

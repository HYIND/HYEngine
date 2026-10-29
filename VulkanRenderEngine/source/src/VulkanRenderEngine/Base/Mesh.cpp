#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/Mesh.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"
#include "VulkanRenderEngine/VKContext.h"


Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices)
	: _vertices(vertices), _indices(indices), needUpdateIndirectDraw(true), _viVersion{ 0 }
{
	_vertices = vertices;
	_indices = indices;
	_uuid = Tool::GenerateSimpleUuid();
}

Mesh::~Mesh()
{}

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
	Change();
}

void Mesh::MakeScale(const glm::vec3& scale)
{
	if (scale.x == 1.0f && scale.y == 1.0f && scale.z == 1.0f)
		return;
	if (scale.x == scale.y && scale.y == scale.z)
	{
		for (auto& vertex : _vertices)
			vertex.Position *= scale;
	}
	else
	{
		glm::mat4 mat = glm::scale(glm::mat4(1.0f), scale);
		PrivateTransform(mat);
	}
	Change();
}

void Mesh::MakeTranslate(const glm::vec3& trans)
{
	if (trans.x == 0.0f && trans.y == 0.0f && trans.z == 0.0f)
		return;
	for (auto& vertex : _vertices)
		vertex.Position += trans;
	Change();
}

void Mesh::MakeRotate(float angle, const glm::vec3& axis)
{
	if (angle == 0.0f)
		return;
	glm::mat4 mat = glm::rotate(glm::mat4(1.0f), glm::radians(angle), axis);
	PrivateTransform(mat);
	Change();
}

void Mesh::MakeTransform(const glm::mat4& mat)
{
	if (mat == glm::mat4(1.0f))
		return;
	PrivateTransform(mat);
	Change();
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

void Mesh::SetNeedUpdateIndirectDraw(bool value)
{
	needUpdateIndirectDraw = value;
}

bool Mesh::GetNeedUpdateIndirectDraw() const
{
	return needUpdateIndirectDraw;
}

void Mesh::PrivateTransform(const glm::mat4& mat)
{
	glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(mat)));
	for (auto& vertex : _vertices) {
		vertex.Position = glm::vec3(mat * glm::vec4(vertex.Position, 1.0f));
		vertex.Normal = glm::normalize(normalMat * vertex.Normal);
		vertex.Tangent = glm::normalize(normalMat * vertex.Tangent);
		vertex.Bitangent = glm::normalize(glm::cross(vertex.Normal, vertex.Tangent));
	}
}

void Mesh::Change()
{
	_viVersion++;
	needUpdateIndirectDraw = true;
	OnChange();
}

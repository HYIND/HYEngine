#include "OpenGLRenderEngine/Base/Mesh.h"
#include "OpenGLRenderEngine/General/IndirectDrawManager.h"
#include "glm/gtc/matrix_transform.hpp"
#include "Helper/Tools.h"
#include <string>

Mesh::Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices)
	:VAO(0), VBO(0), EBO(0),
	needSetup(true), needUpdateIndirectDraw(false), needUpdateBVH(true), needUpdateAABB(true),
	_viVersion{ 0 }, _bvhversion{ 0 }, _aabbversion{ 0 }
{
	_uuid = Tool::GenerateSimpleUuid();

	this->vertices = vertices;
	this->indices = indices;

	SetDirty();
}

Mesh::~Mesh()
{
	auto guard = THREADCONTEXT->GetBindGuard();
	if (VAO != 0)
		glDeleteVertexArrays(1, &VAO);
	if (VBO != 0)
		glDeleteBuffers(1, &VBO);
	if (EBO != 0)
		glDeleteBuffers(1, &EBO);

	IndirectDrawManager::Instance()->deleteMesh(*this);
}

void Mesh::Draw(Shader& shader)
{
	Need();

	glBindVertexArray(VAO);
	glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(indices.size()), GL_UNSIGNED_INT, 0);
	glBindVertexArray(0);
}

void Mesh::DrawInstanced(Shader& shader, GLsizei count)
{
	Need();

	glBindVertexArray(VAO);
	glDrawElementsInstanced(GL_TRIANGLES, static_cast<unsigned int>(indices.size()), GL_UNSIGNED_INT, 0, count);
	glBindVertexArray(0);
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
	std::vector<glm::vec3> tangents(vertices.size(), glm::vec3(0.0f));
	std::vector<glm::vec3> bitangents(vertices.size(), glm::vec3(0.0f));

	// 遍历每个三角形
	for (size_t i = 0; i < indices.size(); i += 3) {
		uint32_t i0 = indices[i];
		uint32_t i1 = indices[i + 1];
		uint32_t i2 = indices[i + 2];

		// 获取三个顶点
		const Vertex& v0 = vertices[i0];
		const Vertex& v1 = vertices[i1];
		const Vertex& v2 = vertices[i2];

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
	for (size_t i = 0; i < vertices.size(); i++) {
		glm::vec3& T = tangents[i];
		glm::vec3& B = bitangents[i];
		glm::vec3& N = vertices[i].Normal;

		// Gram-Schmidt 正交化：让T垂直于N
		T = glm::normalize(T - N * glm::dot(N, T));

		// 计算副切线：B = N × T（使用右手坐标系）
		// 注意：有些引擎用 B = cross(N, T)，取决于UV方向
		B = glm::normalize(glm::cross(N, T));

		// 可选：计算手性（handedness），用于法线贴图翻转
		// float handedness = (glm::dot(glm::cross(N, T), B) < 0.0f) ? -1.0f : 1.0f;

		vertices[i].Tangent = T;
		vertices[i].Bitangent = B;
	}

	SetDirty();
}

void Mesh::MakeScale(const glm::vec3& scale)
{
	if (scale.x == 1.0f && scale.y == 1.0f && scale.z == 1.0f)
		return;
	if (scale.x == scale.y && scale.y == scale.z)
	{
		for (auto& vertex : vertices)
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
	for (auto& vertex : vertices)
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
	for (auto& vertex : vertices) {
		vertex.Position = glm::vec3(mat * glm::vec4(vertex.Position, 1.0f));
		vertex.Normal = glm::normalize(normalMat * vertex.Normal);
		vertex.Tangent = glm::normalize(normalMat * vertex.Tangent);
		vertex.Bitangent = glm::normalize(glm::cross(vertex.Normal, vertex.Tangent));
	}

	SetDirty();
}

std::shared_ptr<Mesh> Mesh::Clone()
{
	auto other = std::make_shared<Mesh>(vertices, indices);
	if (bvhdata) other->bvhdata = std::make_shared<BVHData>(*bvhdata);
	return other;
}

std::shared_ptr<const BVHData> Mesh::GetBVHData()
{
	if (needUpdateBVH || !bvhdata)
	{
		std::vector<AABB> triangleAABBData;

		for (int i = 0; i < indices.size(); i += 3)
		{
			AABB aabb;
			aabb.extend(vertices[indices[i + 0]].Position);
			aabb.extend(vertices[indices[i + 1]].Position);
			aabb.extend(vertices[indices[i + 2]].Position);

			triangleAABBData.push_back(aabb);
		}

		bvhdata = BVHBuilder::Build(triangleAABBData, OpenGLRenderConfig::Mesh_BVH_Leaf_TriCount, OpenGLRenderConfig::RayTrace_Max_Recursive_Depth);
		needUpdateBVH = false;
	}
	return bvhdata;
}


AABB Mesh::GetAABB()
{
	if (needUpdateAABB)
	{
		AABB aabb;
		for (auto& v : vertices)
			aabb.extend(v.Position);

		aabbdata = aabb;
		needUpdateAABB = false;
	}
	return aabbdata;
}

const std::vector<Vertex>& Mesh::GetVertices() const
{
	return vertices;
}

const std::vector<unsigned int>& Mesh::GetIndices() const
{
	return indices;
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
	auto guard = THREADCONTEXT->GetBindGuard();

	if (VBO == 0)
		glGenBuffers(1, &VBO);
	if (EBO == 0)
		glGenBuffers(1, &EBO);

	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

	if (VAO != 0)
	{
		glDeleteVertexArrays(1, &VAO);
		VAO = 0;
	}

	glGenVertexArrays(1, &VAO);
	glBindVertexArray(VAO);

	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);

	// 顶点位置
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
	// 顶点法线
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));
	// 顶点纹理坐标
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
	// 切线
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));
	// 副切线
	glEnableVertexAttribArray(4);
	glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));

	// 骨骼
	glEnableVertexAttribArray(5);
	glVertexAttribIPointer(5, 4, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, m_BoneIDs));

	// 骨骼
	glEnableVertexAttribArray(6);
	glVertexAttribIPointer(6, 4, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, m_BoneIDs[4]));

	// 骨骼权重
	glEnableVertexAttribArray(7);
	glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, m_Weights));

	// 骨骼权重
	glEnableVertexAttribArray(8);
	glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, m_Weights[4]));


	needSetup = false;
}

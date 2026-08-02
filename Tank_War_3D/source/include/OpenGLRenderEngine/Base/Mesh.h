#pragma once

#include "OpenGLRenderEngine/Base/Shader.h"
#include "OpenGLRenderEngine/General/BVHBuilder.h"
#include "OpenGLRenderEngine/OpenGLRenderConfig.h"
#include <glm\glm.hpp>
#include <vector>
#include "Manager/RenderContextManager.h"
#include <atomic>

struct alignas(16) Vertex {
	alignas(16) glm::vec3 Position;
	alignas(16) glm::vec3 Normal;
	alignas(16) glm::vec2 TexCoords;
	alignas(16) glm::vec3 Tangent;
	alignas(16) glm::vec3 Bitangent;
	alignas(16) int m_BoneIDs[OpenGLRenderConfig::Mesh_Max_Bone_Influence] = { -1 };
	alignas(16) float m_Weights[OpenGLRenderConfig::Mesh_Max_Bone_Influence] = { 1.0f };
};

class Mesh
{
public:
	Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices);
	~Mesh();

	void Draw(Shader& shader);
	void DrawInstanced(Shader& shader, GLsizei count);

	void CalculateTangentData();	//计算切线和副切线

	void MakeScale(const glm::vec3& scale, bool needSetupMesh = true);
	void MakeTranslate(const glm::vec3& trans, bool needSetupMesh = true);
	void MakeRotate(float angle, const glm::vec3& axis, bool needSetupMesh = true);
	void MakeTransform(const glm::mat4& mat, bool needSetupMesh = true);

	std::shared_ptr<Mesh> Clone();

	std::shared_ptr<BVHData>& GetBVHData();
	AABB GetAABB();

	std::vector<Vertex>& GetVertices();
	std::vector<unsigned int>& GetIndices();

	void setupMesh();

	std::string GetUUID();

	uint32_t GetVerticesIndicesVsrsion();
	uint32_t GetBVHDataVsrsion();
	uint32_t GetAABBDataVsrsion();
private:
	unsigned int VAO, VBO, EBO;

	bool InitVAO = false;
	bool InitVBO = false;
	bool InitEBO = false;

	void Need();

private:
	std::vector<Vertex> vertices;
	std::vector<unsigned int> indices;

	std::shared_ptr<BVHData> bvhdata;
	AABB aabbdata;

	bool needUpdateVerticesIndices;
	bool needUpdateBVH;
	bool needUpdateAABB;
	std::atomic<uint32_t> _viVersion;
	std::atomic<uint32_t> _bvhversion;
	std::atomic<uint32_t> _aabbversion;

	std::string _uuid;
};
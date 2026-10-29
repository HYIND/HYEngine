#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/VKWrapper/VmaBuffer.h"

struct alignas(16) Vertex
{
	alignas(16) glm::vec3 Position;
	alignas(16) glm::vec3 Normal;
	alignas(16) glm::vec2 TexCoords;
	alignas(16) glm::vec3 Tangent;
	alignas(16) glm::vec3 Bitangent;
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

	const std::vector<Vertex>& GetVertices() const;
	const std::vector<unsigned int>& GetIndices() const;

	std::string GetUUID();
	uint32_t GetVerticesIndicesVsrsion();

public:
	void SetNeedUpdateIndirectDraw(bool value);
	bool GetNeedUpdateIndirectDraw() const;

protected:
	virtual void OnChange() {};

private:
	void PrivateTransform(const glm::mat4& mat);
	void Change();

protected:
	std::vector<Vertex> _vertices;
	std::vector<unsigned int> _indices;

	bool needUpdateIndirectDraw;
	std::atomic<uint32_t> _viVersion;
	std::string _uuid;
};
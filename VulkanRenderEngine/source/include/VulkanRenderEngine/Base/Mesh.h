#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/General/BVHBuilder.h"
#include "VulkanRenderEngine/GlobalConfig.h"
#include "VulkanRenderEngine/VKContext.h"
#include "VulkanRenderEngine/VKWrapper/VmaBuffer.h"


struct alignas(16) Vertex {
	alignas(16) glm::vec3 Position;
	alignas(16) glm::vec3 Normal;
	alignas(16) glm::vec2 TexCoords;
	alignas(16) glm::vec3 Tangent;
	alignas(16) glm::vec3 Bitangent;
	alignas(16) int m_BoneIDs[GlobalConfig::Mesh_Max_Bone_Influence] = { -1 };
	alignas(16) float m_Weights[GlobalConfig::Mesh_Max_Bone_Influence] = { 1.0f };

	static std::vector<vk::VertexInputAttributeDescription> GetVertexInputAttributeDescription(uint32_t binding = 0, uint32_t locaitonOffset = 0);
	static vk::VertexInputBindingDescription GetVertexInputBindingDescription(uint32_t binding = 0);
};

class Mesh
{
public:
	Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);
	~Mesh();

	void Draw(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd);
	void DrawInstanced(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd, uint32_t count);

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
	bool GetNeedUpdateIndricetDraw() const;

private:
	VKCore::VulkanDevice* _device;
	std::unique_ptr<VKWrapper::VmaBuffer> _vertexBuffer;
	std::unique_ptr<VKWrapper::VmaBuffer> _indexBuffer;

	void Need();
	void SetupMesh();

	void UpdateGPUData();

private:
	std::vector<Vertex> _vertices;
	std::vector<unsigned int> _indices;

	std::shared_ptr<BVHData> _bvhdata;
	AABB _aabbdata;

	bool needSetup;
	bool needUpdateIndirectDraw;
	bool needUpdateBVH;
	bool needUpdateAABB;
	std::atomic<uint32_t> _viVersion;
	std::atomic<uint32_t> _bvhversion;
	std::atomic<uint32_t> _aabbversion;

	std::string _uuid;
};
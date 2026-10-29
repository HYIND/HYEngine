#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/General/BVHBuilder.h"
#include "Mesh.h"


class StaticMesh :public Mesh
{
public:
	static std::vector<vk::VertexInputAttributeDescription> GetVertexInputAttributeDescription(uint32_t baseBinding = 0, uint32_t locaitonOffset = 0);
	static vk::VertexInputBindingDescription GetVertexInputBindingDescription(uint32_t baseBinding = 0);

public:
	StaticMesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);
	~StaticMesh();

	std::shared_ptr<StaticMesh> Clone();

	std::shared_ptr<const BVHData> GetBVHData();
	AABB GetAABB();

	uint32_t GetBVHDataVsrsion();
	uint32_t GetAABBDataVsrsion();

	void SetDirty();

protected:
	virtual void OnChange() override;;

private:
	std::shared_ptr<BVHData> _bvhdata;
	AABB _aabbdata;

	bool needUpdateBVH;
	bool needUpdateAABB;
	std::atomic<uint32_t> _bvhversion;
	std::atomic<uint32_t> _aabbversion;
};
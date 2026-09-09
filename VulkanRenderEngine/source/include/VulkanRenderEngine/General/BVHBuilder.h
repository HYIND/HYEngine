#pragma once

#include <glm/glm.hpp>
#include <memory>

struct alignas(16) AABB
{
	alignas(16) glm::vec3 min;
	alignas(16) glm::vec3 max;

	AABB();
	AABB(const AABB& other);
	AABB(const glm::vec3& min, const glm::vec3& max);

	void extend(const glm::vec3& point);
	void extend(const AABB& other);
	bool isValid() const;
	bool isInside(const glm::vec3& point) const;
	float DistancePointToAABB(const glm::vec3& point);
	float DistancePointToAABBSqrt(const glm::vec3& point);
	void MakeTransform(const glm::mat4& mat);
	glm::vec3 GetCenter() const;
};

struct BVHNode {
	AABB aabb;
	int leftChild;   // 子节点索引，-1 表示叶子
	int rightChild;  // 子节点索引，-1 表示叶子
	int idFirst = 0;
	int idCount = 0;
};

struct BVHData
{
	std::vector<BVHNode> nodes;
	std::vector<int> indices;

	BVHData();
	BVHData(BVHData& other);
};

class BVHBuilder
{
public:
	static std::shared_ptr<BVHData> Build(const std::vector<AABB>& aabbs, size_t leaf_Object_Limit, size_t max_Depth);

private:
	static int BuildNode(std::shared_ptr<BVHData>& data, const std::vector<AABB>& aabbs, int start, int end, size_t leaf_Object_Limit, size_t cur_Depth, size_t max_Depth);
	static int ChooseAxis(std::shared_ptr<BVHData>& data, const std::vector<AABB>& aabbs, int start, int end);
	static AABB ComputeAABB(std::shared_ptr<BVHData>& data, const std::vector<AABB>& aabbs, int start, int end);
};
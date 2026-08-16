#include "OpenGLRenderEngine/General/BVHBuilder.h"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/norm.hpp"
#include <algorithm>
#include <array> 

AABB::AABB() :min(glm::vec3(std::numeric_limits<float>::max())), max(glm::vec3(-std::numeric_limits<float>::max())) {}

AABB::AABB(const AABB& other)
{
	min = other.min;
	max = other.max;
}

AABB::AABB(const glm::vec3& min, const glm::vec3& max) : min(min), max(max) {}

void AABB::extend(const glm::vec3& point) {
	min = glm::min(min, point);
	max = glm::max(max, point);
}

void AABB::extend(const AABB& other) {
	min = glm::min(min, other.min);
	max = glm::max(max, other.max);
}

bool AABB::isInside(const glm::vec3& point) const
{
	return (point.x >= min.x && point.x <= max.x) &&
		(point.y >= min.y && point.y <= max.y) &&
		(point.z >= min.z && point.z <= max.z);
}

float AABB::DistancePointToAABB(const glm::vec3& point) {
	glm::vec3 closestPoint;
	closestPoint.x = glm::clamp(point.x, min.x, max.x);
	closestPoint.y = glm::clamp(point.y, min.y, max.y);
	closestPoint.z = glm::clamp(point.z, min.z, max.z);

	return glm::distance(point, closestPoint);
}

float AABB::DistancePointToAABBSqrt(const glm::vec3& point) {
	glm::vec3 closestPoint;
	closestPoint.x = glm::clamp(point.x, min.x, max.x);
	closestPoint.y = glm::clamp(point.y, min.y, max.y);
	closestPoint.z = glm::clamp(point.z, min.z, max.z);

	return glm::length2(point - closestPoint);
}

void AABB::MakeTransform(const glm::mat4& mat)
{
	std::array<glm::vec3, 8> corners = {
		glm::vec3(min.x, min.y, min.z),
		glm::vec3(max.x, min.y, min.z),
		glm::vec3(min.x, max.y, min.z),
		glm::vec3(max.x, max.y, min.z),
		glm::vec3(min.x, min.y, max.z),
		glm::vec3(max.x, min.y, max.z),
		glm::vec3(min.x, max.y, max.z),
		glm::vec3(max.x, max.y, max.z)
	};

	// 初始化世界空间AABB
	glm::vec3 transformMin = glm::vec3(FLT_MAX);
	glm::vec3 transformMax = glm::vec3(-FLT_MAX);

	// 变换所有顶点并更新AABB
	for (const auto& corner : corners) {
		glm::vec4 worldCorner = mat * glm::vec4(corner, 1.0f);
		transformMin = glm::min(transformMin, glm::vec3(worldCorner));
		transformMax = glm::max(transformMax, glm::vec3(worldCorner));
	}

	min = transformMin;
	max = transformMax;
}

glm::vec3 AABB::GetCenter() const
{
	return min + (max - min) / 2.f;
}

bool AABB::isValid() const {
	return min.x <= max.x && min.y <= max.y && min.z <= max.z;
}

BVHData::BVHData()
{
}

BVHData::BVHData(BVHData& other)
{
	this->nodes = other.nodes;
	this->indices = other.indices;
}

std::shared_ptr<BVHData> BVHBuilder::Build(const std::vector<AABB>& aabbs, size_t leaf_Object_Limit, size_t max_Depth)
{
	auto data = std::make_shared<BVHData>();
	if (aabbs.empty()) return data;

	data->indices.resize(aabbs.size());
	for (int i = 0; i < aabbs.size(); i++) {
		data->indices[i] = i;
	}

	leaf_Object_Limit = std::max((size_t)1, leaf_Object_Limit);
	BuildNode(data, aabbs, 0, aabbs.size(), leaf_Object_Limit, 0, max_Depth);
	return data;
}

int BVHBuilder::ChooseAxis(std::shared_ptr<BVHData>& data, const std::vector<AABB>& aabbs, int start, int end)
{
	AABB total = ComputeAABB(data, aabbs, start, end);
	glm::vec3 extent = total.max - total.min;

	if (extent.x >= extent.y && extent.x >= extent.z) return 0;
	if (extent.y >= extent.x && extent.y >= extent.z) return 1;
	return 2;
}

AABB BVHBuilder::ComputeAABB(std::shared_ptr<BVHData>& data, const std::vector<AABB>& aabbs, int start, int end)
{
	AABB result;
	for (int i = start; i < end; i++) {
		result.extend(aabbs[data->indices[i]]);
	}
	return result;
}

int BVHBuilder::BuildNode(std::shared_ptr<BVHData>& data, const std::vector<AABB>& aabbs, int start, int end, size_t leaf_Object_Limit, size_t cur_Depth, size_t max_Depth) {
	BVHNode node;
	node.aabb = ComputeAABB(data, aabbs, start, end);

	int count = end - start;

	// 如果少于 4 个，创建叶子节点
	if (count <= leaf_Object_Limit || cur_Depth >= max_Depth) {
		node.leftChild = -1;
		node.rightChild = -1;
		node.idFirst = start;  // 存储 mesh 索引
		node.idCount = count;
		data->nodes.push_back(node);
		std::sort(data->indices.begin() + start, data->indices.begin() + end,
			[](int a, int b) {
				return a < b;
			});
		return data->nodes.size() - 1;
	}

	// 选择分割轴
	int axis = ChooseAxis(data, aabbs, start, end);

	// 按中心点排序
	std::sort(data->indices.begin() + start, data->indices.begin() + end,
		[&aabbs, axis](int a, int b) {
			float ca = (aabbs[a].min[axis] + aabbs[a].max[axis]) * 0.5f;
			float cb = (aabbs[b].min[axis] + aabbs[b].max[axis]) * 0.5f;
			return ca < cb;
		});

	// 中位数分割
	int mid = (start + end) / 2;

	// 递归构建
	int left = BuildNode(data, aabbs, start, mid, leaf_Object_Limit, cur_Depth + 1, max_Depth);
	int right = BuildNode(data, aabbs, mid, end, leaf_Object_Limit, cur_Depth + 1, max_Depth);

	node.leftChild = left;
	node.rightChild = right;
	data->nodes.push_back(node);
	return data->nodes.size() - 1;
}

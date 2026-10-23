#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "glm/glm.hpp"

using SkeletonNodeIndex = uint32_t;

struct SkeletonNode
{
	static constexpr SkeletonNodeIndex InvalidIndex = std::numeric_limits<SkeletonNodeIndex>::max();

	SkeletonNodeIndex parentIndex = InvalidIndex;
	std::vector<SkeletonNodeIndex> childIndices;
	std::string name;
	std::string path;
	glm::mat4 bindLocalTransform{ 1.0f };
};

struct Skeleton
{
	std::vector<SkeletonNode> nodes;
	std::unordered_map<std::string, SkeletonNodeIndex> pathToNode;
	std::unordered_map<std::string, std::vector<SkeletonNodeIndex>> nameToNodes;

	SkeletonNodeIndex AddNode(const std::string& name, const std::string& path, SkeletonNodeIndex parentIndex, const glm::mat4& bindLocalTransform);
	SkeletonNodeIndex FindNodeByPath(const std::string& path) const;
	SkeletonNodeIndex FindUniqueNodeByName(const std::string& name) const;

	std::shared_ptr<Skeleton> Clone() const;
};

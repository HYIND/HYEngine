#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/Skeleton.h"

SkeletonNodeIndex Skeleton::AddNode(const std::string& name, const std::string& path, SkeletonNodeIndex parentIndex, const glm::mat4& bindLocalTransform)
{
	const SkeletonNodeIndex nodeIndex = static_cast<SkeletonNodeIndex>(nodes.size());
	SkeletonNode node;
	node.parentIndex = parentIndex;
	node.name = name;
	node.path = path;
	node.bindLocalTransform = bindLocalTransform;
	nodes.emplace_back(std::move(node));

	pathToNode[path] = nodeIndex;
	nameToNodes[name].push_back(nodeIndex);
	if (parentIndex != SkeletonNode::InvalidIndex && parentIndex < nodeIndex)
		nodes[parentIndex].childIndices.push_back(nodeIndex);
	return nodeIndex;
}

SkeletonNodeIndex Skeleton::FindNodeByPath(const std::string& path) const
{
	auto it = pathToNode.find(path);
	return it == pathToNode.end() ? SkeletonNode::InvalidIndex : it->second;
}

SkeletonNodeIndex Skeleton::FindUniqueNodeByName(const std::string& name) const
{
	auto it = nameToNodes.find(name);
	if (it == nameToNodes.end() || it->second.size() != 1)
		return SkeletonNode::InvalidIndex;
	return it->second.front();
}

std::shared_ptr<Skeleton> Skeleton::Clone() const
{
	auto other = std::make_shared<Skeleton>();
	other->nodes = nodes;
	other->pathToNode = pathToNode;
	other->nameToNodes = nameToNodes;
	return other;
}

#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/Skeleton.h"
#include "VulkanRenderEngine/Base/AssimpGlmHelpers.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <algorithm>
#include <iostream>

namespace
{
	void AddNodeRecursive(Skeleton& skeleton, SkeletonIndex& index, aiNode* node, SkeletonNodeIndex parentIndex, const std::string& path)
	{
		if (!node)
			return;

		const SkeletonNodeIndex nodeIndex = skeleton.AddNode(
			node->mName.C_Str(),
			path,
			parentIndex,
			AssimpGLMHelpers::ConvertMatrixToGLMFormat(node->mTransformation));
		index.nodeIds.emplace(node, nodeIndex);

		for (unsigned int i = 0; i < node->mNumChildren; ++i)
		{
			aiNode* child = node->mChildren[i];
			if (!child)
				continue;
			// 兄弟下标进 path：导出器常有重名节点（joint/joint1），光靠名字查不到唯一节点
			AddNodeRecursive(skeleton, index, child, nodeIndex, path + "/" + child->mName.C_Str() + "[" + std::to_string(i) + "]");
		}
	}
}

SkeletonIndex BuildSkeletonIndex(aiNode* rootNode)
{
	SkeletonIndex index;
	if (!rootNode)
		return index;

	AddNodeRecursive(index.skeleton, index, rootNode, SkeletonNode::InvalidIndex, "/" + std::string(rootNode->mName.C_Str()));

	// 父节点一定先于子节点建好（parent < child），所以一趟顺序遍历就能累积出 globalBind
	Skeleton& skeleton = index.skeleton;
	std::vector<glm::mat4> globalBind(skeleton.nodes.size(), glm::mat4(1.0f));
	for (SkeletonNodeIndex i = 0; i < skeleton.nodes.size(); ++i)
	{
		SkeletonNode& node = skeleton.nodes[i];
		globalBind[i] = node.parentIndex == SkeletonNode::InvalidIndex
			? node.bindLocalTransform
			: globalBind[node.parentIndex] * node.bindLocalTransform;
		node.inverseBindTransform = glm::inverse(globalBind[i]);
	}

	return index;
}

std::shared_ptr<Skeleton> LoadSkeleton(const std::string& path)
{
	std::string replacePath = path;
	std::replace(replacePath.begin(), replacePath.end(), '\\', '/');

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(replacePath, aiProcess_Triangulate | aiProcess_PopulateArmatureData);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		std::cerr << "ERROR::ASSIMP:: " << importer.GetErrorString() << " (" << replacePath << ")" << std::endl;
		return nullptr;
	}

	SkeletonIndex index = BuildSkeletonIndex(scene->mRootNode);
	auto skeleton = std::make_shared<Skeleton>(std::move(index.skeleton));

	// 导出器自己算的逆绑定矩阵比 inverse(globalBind) 可靠：骨架根节点带缩放时两者会差一截。
	// 按 aiNode 指针取，重名骨骼也不会串。
	for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
	{
		const aiMesh* mesh = scene->mMeshes[meshIndex];
		if (!mesh)
			continue;

		for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
		{
			const aiBone* bone = mesh->mBones[boneIndex];
			if (!bone || !bone->mNode)
				continue;

			const auto it = index.nodeIds.find(bone->mNode);
			if (it == index.nodeIds.end())
				continue;

			skeleton->nodes[it->second].inverseBindTransform = AssimpGLMHelpers::ConvertMatrixToGLMFormat(bone->mOffsetMatrix);
		}
	}

	return skeleton;
}

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

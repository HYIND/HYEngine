#pragma once

#include "glm/glm.hpp"

struct BoneInfo
{
	int id;				/*id is index in finalBoneMatrices*/
	glm::mat4 offset;	/*offset matrix transforms vertex from model space to bone space*/
};

struct Skeleton
{
	std::map<std::string, BoneInfo> BoneInfoMap;
	int BoneCounter = 0;

	std::shared_ptr<Skeleton> Clone()
	{
		auto other = std::make_shared<Skeleton>();
		other->BoneInfoMap = BoneInfoMap;
		other->BoneCounter = BoneCounter;
		return other;
	}
};
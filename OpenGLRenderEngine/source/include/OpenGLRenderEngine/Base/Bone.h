#pragma once

/* Container for bone data */

#include <vector>
#include <assimp/scene.h>
#include <list>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include "OpenGLRenderEngine/Base/AssimpGlmHelpers.h"

struct KeyPosition
{
	glm::vec3 position;
	float timeStamp;
};

struct KeyRotation
{
	glm::quat orientation;
	float timeStamp;
};

struct KeyScale
{
	glm::vec3 scale;
	float timeStamp;
};

class Bone
{
public:
	Bone(const std::string& name, const aiNodeAnim* channel);

	glm::mat4 GetLocalTransform(float animationTime) const;
	std::string GetBoneName() const;

private:
	int GetPositionIndex(float animationTime) const;
	int GetRotationIndex(float animationTime) const;
	int GetScaleIndex(float animationTime) const;

	glm::mat4 InterpolatePosition(float animationTime) const;
	glm::mat4 InterpolateRotation(float animationTime) const;
	glm::mat4 InterpolateScaling(float animationTime) const;


private:
	std::vector<KeyPosition> m_Positions;
	std::vector<KeyRotation> m_Rotations;
	std::vector<KeyScale> m_Scales;
	int m_NumPositions;
	int m_NumRotations;
	int m_NumScalings;

	std::string m_Name;
};
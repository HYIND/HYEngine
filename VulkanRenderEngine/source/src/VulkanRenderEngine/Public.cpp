#include "vkstdafx.h"
#include "VulkanRenderEngine/Public.h"

glm::mat4 vkPerspective(float radians, float aspect, float nearplane, float farplane)
{
	auto mat = glm::perspective(radians, aspect, nearplane, farplane);
	mat[1][1] *= -1.0f;
	return mat;
}


glm::mat4 vkOrtho(float left, float right, float bottom, float top, float zNear, float zFar)
{
	auto mat = glm::ortho(left, right, bottom, top, zNear, zFar);
	mat[1][1] *= -1.0f;
	return mat;
}


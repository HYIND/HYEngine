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
	// Vulkan 的 Y 轴向下：翻转要把整个 y 行取反，即缩放项 [1][1] 和平移项 [3][1] 一起翻。
	// 只翻 [1][1] 时，bottom / top 不对称（不以 0 为中心）的投影会整体平移 2·(t+b)/(t-b)，
	// 一部分范围被推出 [-1,1]：级联阴影里表现为某些角度下一条带子没有阴影（视锥切片的光空间 AABB 中心不在原点）
	mat[1][1] *= -1.0f;
	mat[3][1] *= -1.0f;
	return mat;
}


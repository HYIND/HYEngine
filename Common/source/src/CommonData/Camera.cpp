#include "CommonData\Camera.h"
#include <glm\gtc\matrix_transform.hpp>
#include <algorithm>
#include <cmath>


bool CameraHelper::IsFinite(const glm::vec3& v)
{
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool CameraHelper::SanitizeBasis(glm::vec3& front, glm::vec3& up, glm::vec3& right)
{
	bool fixed = false;

	const float frontLength = IsFinite(front) ? glm::length(front) : 0.f;
	if (!(frontLength > 1e-6f))
	{
		front = glm::vec3(0.f, 0.f, -1.f);
		fixed = true;
	}
	else
		front /= frontLength;

	glm::vec3 reference = up;
	const float upLength = IsFinite(reference) ? glm::length(reference) : 0.f;
	if (!(upLength > 1e-6f))
	{
		reference = glm::vec3(0.f, 1.f, 0.f);
		fixed = true;
	}
	else
		reference /= upLength;

	glm::vec3 newRight = glm::cross(front, reference);
	float rightLength = glm::length(newRight);
	if (!(rightLength > 1e-4f))
	{
		// front 和 up 平行（正上/正下看）：换一根不平行的参考轴
		reference = std::abs(front.y) < 0.999f ? glm::vec3(0.f, 1.f, 0.f) : glm::vec3(0.f, 0.f, 1.f);
		newRight = glm::cross(front, reference);
		rightLength = glm::length(newRight);
		fixed = true;
	}

	right = newRight / rightLength;
	up = glm::cross(right, front);	// front、right 都是单位且正交，结果本身就是单位向量
	return fixed;
}

bool CameraHelper::SanitizeProjection(float& aspect, float& fov, float& nearPlane, float& farPlane)
{
	bool fixed = false;
	if (!std::isfinite(aspect) || aspect <= 1e-6f)
	{
		aspect = 1.f;
		fixed = true;
	}
	if (!std::isfinite(fov) || fov <= 1e-3f || fov >= 179.f)
	{
		fov = 80.f;
		fixed = true;
	}
	if (!std::isfinite(nearPlane) || nearPlane <= 0.f)
	{
		nearPlane = 0.1f;
		fixed = true;
	}
	if (!std::isfinite(farPlane) || farPlane <= nearPlane * 1.0001f)
	{
		farPlane = std::max(500.f, nearPlane * 10.f);
		fixed = true;
	}
	return fixed;
}

glm::mat4 CameraHelper::GetPerspectiveProjectionMatrix(float aspect, float fov, float nearPlane, float farPlane)
{
	SanitizeProjection(aspect, fov, nearPlane, farPlane);
	auto proj = glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
	proj[1][1] *= -1.0f;
	return proj;
}

glm::mat4 CameraHelper::GetPerspectiveProjectionMatrix(uint32_t width, uint32_t height, float fov, float nearPlane, float farPlane)
{
	// height 为 0 时除出来是 inf，交给 SanitizeProjection 换成 1
	return GetPerspectiveProjectionMatrix(height > 0 ? (float)width / (float)height : 0.f, fov, nearPlane, farPlane);
}

glm::mat4 CameraHelper::GetViewMatrix(const glm::vec3& position, const glm::vec3 cameraFront, const glm::vec3 worldUp)
{
	glm::vec3 front = cameraFront;
	glm::vec3 up = worldUp;
	glm::vec3 right;
	SanitizeBasis(front, up, right);
	const glm::vec3 eye = IsFinite(position) ? position : glm::vec3(0.f);
	return glm::lookAt(eye, eye + front, up);
}

CameraFrontData::CameraFrontData(float yaw, float pitch, const glm::vec3& worldUp) {
	updateCameraVectors(yaw, pitch, worldUp);
}

CameraFrontData::CameraFrontData(const glm::vec3& front, const glm::vec3& worldUp)
{
	float yaw, pitch;
	// 非有限的朝向（比如零四元数 normalize 出来的 NaN）按 (0,0,-1) 处理
	const glm::vec3 dir = CameraHelper::IsFinite(front) ? front : glm::vec3(0.f, 0.f, -1.f);
	if (std::abs(dir.x) < 1e-6f && std::abs(dir.z) < 1e-6f)
		yaw = 90.f;
	else
		yaw = glm::degrees(atan2(dir.z, dir.x));
	pitch = glm::degrees(atan2(dir.y, sqrt(dir.x * dir.x + dir.z * dir.z)));

	updateCameraVectors(yaw, pitch, worldUp);
}

void CameraFrontData::updateCameraVectors(float yaw, float pitch, const glm::vec3 worldUp)
{
	if (!std::isfinite(yaw))
		yaw = -90.f;
	if (!std::isfinite(pitch))
		pitch = 0.f;
	if (pitch > 89.0f)
		pitch = 89.0f;
	if (pitch < -89.0f)
		pitch = -89.0f;

	glm::vec3 newfront;
	newfront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
	newfront.y = sin(glm::radians(pitch));
	newfront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));

	// worldUp 为零或与 front 平行时原写法会 normalize 出 NaN，统一走 SanitizeBasis
	cameraFront = newfront;
	cameraUp = worldUp;
	CameraHelper::SanitizeBasis(cameraFront, cameraUp, cameraRight);
}

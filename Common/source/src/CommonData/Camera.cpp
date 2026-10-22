#include "CommonData\Camera.h"
#include <glm\gtc\matrix_transform.hpp>


glm::mat4 CameraHelper::GetPerspectiveProjectionMatrix(float aspect, float fov, float nearPlane, float farPlane)
{
	auto proj = glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
	proj[1][1] *= -1.0f;
	return proj;
}

glm::mat4 CameraHelper::GetPerspectiveProjectionMatrix(uint32_t width, uint32_t height, float fov, float nearPlane, float farPlane)
{
	return GetPerspectiveProjectionMatrix((float)width / (float)height, fov, nearPlane, farPlane);
}

glm::mat4 CameraHelper::GetViewMatrix(const glm::vec3& position, const glm::vec3 cameraFront, const glm::vec3 worldUp)
{
	auto cameraRight = glm::normalize(glm::cross(cameraFront, worldUp));
	auto cameraUp = glm::normalize(glm::cross(cameraRight, cameraFront));
	return glm::lookAt(position, position + cameraFront, cameraUp);
}

CameraFrontData::CameraFrontData(float yaw, float pitch, const glm::vec3& worldUp) {
	updateCameraVectors(yaw, pitch, worldUp);
}

CameraFrontData::CameraFrontData(const glm::vec3& front, const glm::vec3& worldUp)
{
	float yaw, pitch;
	if (std::abs(front.x) < 1e-6f && std::abs(front.z) < 1e-6f)
		yaw = 90.f;
	else
		yaw = glm::degrees(atan2(front.z, front.x));
	pitch = glm::degrees(atan2(front.y, sqrt(front.x * front.x + front.z * front.z)));

	updateCameraVectors(yaw, pitch, worldUp);
}

void CameraFrontData::updateCameraVectors(float yaw, float pitch, const glm::vec3 worldUp)
{
	if (pitch > 89.0f)
		pitch = 89.0f;
	if (pitch < -89.0f)
		pitch = -89.0f;

	glm::vec3 newfront;
	newfront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
	newfront.y = sin(glm::radians(pitch));
	newfront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));

	cameraFront = glm::normalize(newfront);
	cameraRight = glm::normalize(glm::cross(cameraFront, worldUp));
	cameraUp = glm::normalize(glm::cross(cameraRight, cameraFront));
}

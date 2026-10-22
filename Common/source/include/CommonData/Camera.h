#pragma once

#include <glm/glm.hpp>

struct CameraFrontData {
	glm::vec3 cameraFront;
	glm::vec3 cameraRight;
	glm::vec3 cameraUp;

	CameraFrontData(float yaw, float pitch, const glm::vec3& worldUp);
	CameraFrontData(const glm::vec3& front, const glm::vec3& worldUp);

private:
	void updateCameraVectors(float yaw, float pitch, const glm::vec3 worldUp);
};

class CameraHelper
{

public:
	static glm::mat4 GetPerspectiveProjectionMatrix(float aspect, float fov, float nearPlane, float farPlane);
	static glm::mat4 GetPerspectiveProjectionMatrix(uint32_t width, uint32_t height, float fov, float nearPlane, float farPlane);
	static glm::mat4 GetViewMatrix(const glm::vec3& position, const glm::vec3 cameraFront, const glm::vec3 worldUp);

};
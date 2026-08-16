#pragma once

#include <glm/glm.hpp>

class Camera
{

public:
	Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, -5.f),
		glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f)
	);

	glm::mat4 GetPerspectiveProjectionMatrix(float aspect) const;
	glm::mat4 GetViewMatrix() const;

	glm::vec3 GetPosition() const;
	glm::vec3 GetDirection() const;
	glm::vec3 GetDirectionUp() const;
	glm::vec3 GetDirectionRight() const;
	float GetYaw() const;
	float GetPitch() const;
	float GetFOV() const;
	float GetNearPlane() const;
	float GetFarPlane() const;

	void SetYaw(float y);
	void SetPitch(float p);
	void SetYawPitch(float y, float p);
	void SetPosition(const glm::vec3& pos);
	void SetDirection(const glm::vec3& dir);
	void SetFOV(float fov);
	void SetNearPlane(float distance);
	void SetFarPlane(float distance);

private:
	void updateCameraVectors();

private:
	glm::vec3 position;
	glm::vec3 worldUp;

	glm::vec3 cameraFront;
	glm::vec3 cameraRight;
	glm::vec3 cameraUp;

	float Yaw;              // 俯仰角
	float Pitch;            // 偏移角

	float fov = 60.0f;
	float nearPlane = 0.1f;
	float farPlane = 200.0f;
};
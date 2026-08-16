#include "CommonData\Camera.h"
#include <glm\gtc\matrix_transform.hpp>

Camera::Camera(glm::vec3 position, glm::vec3 target, glm::vec3 worldUp)
	:position(position), worldUp(worldUp)
{

	glm::vec3 front = glm::normalize(target - position);

	if (std::abs(front.x) < 1e-6f && std::abs(front.z) < 1e-6f)
		this->Yaw = 90.f;
	else
		this->Yaw = glm::degrees(atan2(front.z, front.x));
	this->Pitch = glm::degrees(atan2(front.y, sqrt(front.x * front.x + front.z * front.z)));

	if (Pitch > 89.0f)
		Pitch = 89.0f;
	if (Pitch < -89.0f)
		Pitch = -89.0f;

	updateCameraVectors();
}

glm::mat4 Camera::GetPerspectiveProjectionMatrix(float aspect) const
{
	return glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
}

glm::mat4 Camera::GetViewMatrix() const
{
	return glm::lookAt(position, position + cameraFront, cameraUp);
}

glm::vec3 Camera::GetPosition() const
{
	return position;
}

glm::vec3 Camera::GetDirection() const
{
	return this->cameraFront;
}

glm::vec3 Camera::GetDirectionUp() const
{
	return this->cameraUp;
}

glm::vec3 Camera::GetDirectionRight() const
{
	return this->cameraRight;
}

float Camera::GetYaw() const
{
	return Yaw;
}

float Camera::GetPitch() const
{
	return Pitch;
}

float Camera::GetFOV() const
{
	return fov;
}

float Camera::GetNearPlane() const
{
	return nearPlane;
}

float Camera::GetFarPlane() const
{
	return farPlane;
}

void Camera::SetYaw(float y)
{
	Yaw = y;
	this->updateCameraVectors();
}

void Camera::SetPitch(float p)
{
	Pitch = p;

	if (Pitch > 89.0f)
		Pitch = 89.0f;
	if (Pitch < -89.0f)
		Pitch = -89.0f;

	this->updateCameraVectors();
}

void Camera::SetYawPitch(float y, float p)
{
	Yaw = y;
	Pitch = p;

	if (Pitch > 89.0f)
		Pitch = 89.0f;
	if (Pitch < -89.0f)
		Pitch = -89.0f;

	this->updateCameraVectors();
}

void Camera::SetPosition(const glm::vec3& pos)
{
	position = pos;
}

void Camera::SetDirection(const glm::vec3& dir)
{
	glm::vec3 front = dir;

	if (std::abs(front.x) < 1e-6f && std::abs(front.z) < 1e-6f)
		this->Yaw = 90.f;
	else
		this->Yaw = glm::degrees(atan2(front.z, front.x));
	this->Pitch = glm::degrees(atan2(front.y, sqrt(front.x * front.x + front.z * front.z)));

	if (Pitch > 89.0f)
		Pitch = 89.0f;
	if (Pitch < -89.0f)
		Pitch = -89.0f;
	
	updateCameraVectors();
}

void Camera::SetFOV(float fov)
{
	this->fov = fov;
}

void Camera::SetNearPlane(float distance)
{
	nearPlane = distance;
}

void Camera::SetFarPlane(float distance)
{
	farPlane = distance;
}

void Camera::updateCameraVectors()
{
	glm::vec3 newfront;
	newfront.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
	newfront.y = sin(glm::radians(Pitch));
	newfront.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));

	this->cameraFront = glm::normalize(newfront);
	this->cameraRight = glm::normalize(glm::cross(this->cameraFront, worldUp));
	this->cameraUp = glm::normalize(glm::cross(this->cameraRight, this->cameraFront));
}

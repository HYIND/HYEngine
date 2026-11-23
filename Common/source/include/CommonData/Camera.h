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

	// NaN 防护：把相机基向量整理成单位正交的 front/up/right。
	// front 为零/非有限 → (0,0,-1)；up 为零/非有限 → (0,1,0)；front 与 up 平行 → 换一根参考轴。
	// 正常输入结果与原算法一致（right = normalize(cross(front, up))，up = cross(right, front)）。返回是否做了修正。
	static bool SanitizeBasis(glm::vec3& front, glm::vec3& up, glm::vec3& right);
	// aspect/fov/near/far 非法（非有限、<=0、far<=near、fov 超出 (0,179)）时换成安全值。返回是否做了修正。
	static bool SanitizeProjection(float& aspect, float& fov, float& nearPlane, float& farPlane);
	static bool IsFinite(const glm::vec3& v);

};
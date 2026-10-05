#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/VKWrapper/VKCommandBuffer.h"

constexpr inline int __default_shadow_side = 1024;

class Light
{
public:
	Light();
	Light(const glm::vec3& color);

public:
	void SetColor(const glm::vec3& c);
	void SetColor(float r, float g, float b);
	void SetColorTemperature(float temp);

	void SetShadowMapSize(uint32_t w);

	void SetCastShadow(bool value);

public:
	glm::vec3 GetColor() const;

	uint32_t GetShadowMapSize() const;

	bool GetCastShadow() const;

protected:
	glm::vec3 _color = glm::vec3(1.0f);
	uint32_t _shadowMapSize = __default_shadow_side;
	bool _castShadow = true;
};

class DirLight :public Light
{
public:
	DirLight(const glm::vec3& direction,
		const glm::vec3& color = glm::vec3(1.0f),
		float luxIntensity = 3);

	DirLight(const glm::vec3& direction,
		float colorTemperature,
		float luxIntensity = 3);

	~DirLight();

	void SetDirection(const glm::vec3& dir);
	void SetDirection(float x, float y, float z);
	void SetIntensity(float lux);
	void SetCascadeLevel(uint32_t level);

	glm::vec3 GetDirection() const;
	float GetIntensity() const;
	glm::mat4 GetLightSpaceMatrix() const;
	glm::mat4 GetLightSpaceMatrixWithFrustumCorners(const glm::mat4& projection, const glm::mat4& view, glm::vec3* center = nullptr, float* radius = nullptr) const;
	uint32_t GetCascadeLevel() const;

private:
	glm::vec3 _direction;
	float _luxIntensity = 1000.f;
	uint32_t _cascadeLevel;	//阴影级联
};

class PointLight :public Light
{
public:
	PointLight(const glm::vec3& position,
		const glm::vec3& color = glm::vec3(1.0f),
		float cdIntensity = 300.f
	);
	PointLight(const glm::vec3& position,
		float colorTemperature,
		float cdIntensity = 300.f
	);

	void Draw(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd);

	~PointLight();

	void SetPosition(const glm::vec3& pos);
	void SetPosition(float x, float y, float z);
	void SetIntensity(float cd);

	glm::vec3 GetPosition() const;
	float GetIntensity() const;
	float GetRadius() const;

private:
	glm::vec3 _position;
	float _cdIntensity;
};

class SpotLight :public Light
{
public:
	SpotLight(const glm::vec3& position,
		const glm::vec3& direction,
		float cutOffAngle,
		float outercutOffAngle,
		const glm::vec3& color = glm::vec3(1.0f),
		float cdIntensity = 300.f
	);
	SpotLight(const glm::vec3& position,
		const glm::vec3& direction,
		float cutOffAngle,
		float outercutOffAngle,
		float colorTemperature,
		float cdIntensity = 300.f
	);

	void Draw(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd);

	~SpotLight();

	void SetPosition(const glm::vec3& pos);
	void SetPosition(float x, float y, float z);
	void SetIntensity(float cd);

	void SetDirection(const glm::vec3& dir);
	void SetDirection(float x, float y, float z);

	void SetCutOffAngle(float cut);
	void SetOuterCutOffAngle(float outerCut);

	glm::vec3 GetPosition() const;
	glm::vec3 GetColor() const;
	float GetIntensity() const;
	glm::vec3 GetDirection() const;
	float GetCutOffAngle() const;
	float GetOuterCutOffAngle() const;

	glm::mat4 GetLightSpaceMatrix() const;
	float GetRadius() const;

private:
	glm::vec3 _position;
	float _cdIntensity;
	glm::vec3 _direction;
	float _cutOffAngle;
	float _outercutOffAngle;
};

#pragma once

#include "OpenGLRenderEngine/Base/Shader.h"
#include <glm/glm.hpp>
#include <gl/glew.h>
#include <memory>

constexpr inline int __default_shadow_side = 1024;

class Light
{
public:
	Light();
	Light(const glm::vec3& color);

public:
	void setColor(const glm::vec3& c);
	void setColor(float r, float g, float b);
	void setColorTemperature(float temp);

	void setShadowMapWidth(uint32_t w);
	void setShadowMapHeight(uint32_t h);

	void setCastShadow(bool value);

public:
	glm::vec3 getColor() const;

	uint32_t getShadowMapWidth() const;
	uint32_t getShadowMapHeight() const;

	bool getCastShadow();

protected:
	glm::vec3 _color = glm::vec3(1.0f);
	uint32_t _shadowMapWidth = __default_shadow_side;
	uint32_t _shadowMapHeight = __default_shadow_side;
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

	void Draw(Shader& shader);

	~DirLight();

	void setDirection(const glm::vec3& dir);
	void setDirection(float x, float y, float z);
	void setIntensity(float lux);
	void setCascadeLevel(int level);

	glm::vec3 getDirection() const;
	float getIntensity() const;
	glm::mat4 getLightSpaceMatrix() const;
	glm::mat4 getLightSpaceMatrixWithFrustumCorners(const glm::mat4& projection, const glm::mat4& view, glm::vec3* center = nullptr, float* radius = nullptr) const;
	int getCascadeLevel();

private:
	glm::vec3 _direction;
	float _luxIntensity = 1000.f;
	int _cascadeLevel;	//阴影级联
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

	void Draw(Shader& shader);

	~PointLight();

	void setPosition(const glm::vec3& pos);
	void setPosition(float x, float y, float z);
	void setIntensity(float cd);

	glm::vec3 getPosition() const;
	float getIntensity() const;
	float getRadius() const;

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

	void Draw(Shader& shader);

	~SpotLight();

	void setPosition(const glm::vec3& pos);
	void setPosition(float x, float y, float z);
	void setIntensity(float cd);

	void setDirection(const glm::vec3& dir);
	void setDirection(float x, float y, float z);

	void setCutOffAngle(float cut);
	void setOuterCutOffAngle(float outerCut);

	glm::vec3 getPosition() const;
	glm::vec3 getColor() const;
	float getIntensity() const;
	glm::vec3 getDirection() const;
	float getCutOffAngle() const;
	float getOuterCutOffAngle() const;

	glm::mat4 getLightSpaceMatrix() const;
	float getRadius() const;

private:
	glm::vec3 _position;
	float _cdIntensity;
	glm::vec3 _direction;
	float _cutOffAngle;
	float _outercutOffAngle;
};

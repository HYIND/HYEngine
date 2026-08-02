#pragma once

#include "stdafx.h"
#include "OpenGLRenderEngine/Base/Shader.h"
#include <gl/glew.h>
#include <memory>

constexpr int __default_shadow_side = 1024;

class DirLight
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

	void setColor(const glm::vec3& c);
	void setColor(float r, float g, float b);
	void setColorTemperature(float temp);

	void setIntensity(float lux);

	void setShadowMapWidth(float w);
	void setShadowMapHeight(float h);

	void setCascadeLevel(int level);

	glm::vec3 getDirection() const;
	glm::vec3 getColor() const;
	float getIntensity() const;

	float getShadowMapWidth() const;
	float getShadowMapHeight() const;

	glm::mat4 getLightSpaceMatrix() const;
	glm::mat4 getLightSpaceMatrixWithFrustumCorners(const glm::mat4& projection, const glm::mat4& view, glm::vec3* center = nullptr, float* radius = nullptr) const;

	int getCascadeLevel();

private:
	float shadowMapWidth = __default_shadow_side;
	float shadowMapHeight = __default_shadow_side;

	glm::vec3 _direction;

	glm::vec3 _color;
	float _luxIntensity = 1000.f;

	int _cascadeLevel;	//阴影级联
};

class PointLight
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

	void setColor(const glm::vec3& c);
	void setColor(float r, float g, float b);
	void setColorTemperature(float temp);

	void setIntensity(float cd);

	void setShadowMapWidth(float w);
	void setShadowMapHeight(float h);

	glm::vec3 getPosition() const;
	glm::vec3 getColor() const;
	float getIntensity() const;

	float getShadowMapWidth() const;
	float getShadowMapHeight() const;

	float getRadius() const;

private:
	float shadowMapWidth = __default_shadow_side;
	float shadowMapHeight = __default_shadow_side;

	glm::vec3 _position;

	glm::vec3 _color;
	float _cdIntensity;
};

class SpotLight
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

	void setColor(const glm::vec3& c);
	void setColor(float r, float g, float b);
	void setColorTemperature(float temp);

	void setIntensity(float cd);

	void setDirection(const glm::vec3& dir);
	void setDirection(float x, float y, float z);

	void setCutOffAngle(float cut);
	void setOuterCutOffAngle(float outerCut);

	void setShadowMapWidth(float w);
	void setShadowMapHeight(float h);

	glm::vec3 getPosition() const;
	glm::vec3 getColor() const;
	float getIntensity() const;
	glm::vec3 getDirection() const;
	float getCutOffAngle() const;
	float getOuterCutOffAngle() const;

	float getShadowMapWidth() const;
	float getShadowMapHeight() const;

	glm::mat4 getLightSpaceMatrix() const;
	float getRadius() const;

private:
	float shadowMapWidth = __default_shadow_side;
	float shadowMapHeight = __default_shadow_side;

	glm::vec3 _position;

	glm::vec3 _color;
	float _cdIntensity;

	glm::vec3 _direction;
	float _cutOffAngle;
	float _outercutOffAngle;
};

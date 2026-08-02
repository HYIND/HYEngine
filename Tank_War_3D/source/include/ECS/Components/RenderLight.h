#pragma once

#include "OpenGLRenderEngine/Base/Light.h"
#include "ECS/Core/IComponent.h"
#include "ECS/Core/Entity.h"

enum class LightType { Dir = 0, Point, Spot };

struct RenderLight : public IComponent
{
	std::shared_ptr<DirLight> dirlight;
	std::shared_ptr<PointLight> pointlight;
	std::shared_ptr<SpotLight> spotlight;

	LightType type = LightType::Dir;

	bool renderCube = false;

	RenderLight() {}
	RenderLight(std::shared_ptr<DirLight> dirlight) : dirlight(dirlight), type(LightType::Dir) {}
	RenderLight(std::shared_ptr<PointLight> pointlight) :pointlight(pointlight), type(LightType::Point) {}
	RenderLight(std::shared_ptr<SpotLight> spotlight) :spotlight(spotlight), type(LightType::Spot) {}

	void SetTransForm(Transform& trans) {
		if (type == LightType::Dir) {
			if (dirlight)dirlight->setDirection(trans.getDirection());
		}
		else if (type == LightType::Point) {
			if (pointlight) pointlight->setPosition(trans.position);
		}
		else if (type == LightType::Spot) {
			if (spotlight) {
				spotlight->setPosition(trans.position);
				spotlight->setDirection(trans.getDirection());
			}
		}
	}
	void SetPosition(const glm::vec3& pos) {
		if (type == LightType::Dir) return;
		else if (type == LightType::Point) if (pointlight) pointlight->setPosition(pos);
		else if (type == LightType::Spot) if (spotlight) spotlight->setPosition(pos);
	}
	void SetDirection(const glm::vec3& direction) {
		if (type == LightType::Dir) if (dirlight) dirlight->setDirection(direction);
		else if (type == LightType::Point) return;
		else if (type == LightType::Spot) if (spotlight) spotlight->setDirection(direction);
	}

};

struct LightFollow : public IComponent
{
	Entity target;
};
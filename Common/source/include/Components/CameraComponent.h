#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include "glm\glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/quaternion.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "ECSCore/IComponent.h"
#include "CommonData/Camera.h"
#include "Transform.h"

struct CameraComponent : public IComponent
{
	Camera camera;
	bool isFirstPerson = true;

	CameraComponent() {};

	void SetTransForm(Transform& trans){
		camera.SetPosition(trans.position);
		camera.SetDirection(trans.getDirection());
	}
	void SetPosition(const glm::vec3& pos){
		camera.SetPosition(pos);
	}
	void SetDirection(const glm::vec3& direction){
		camera.SetDirection(direction);
	}
};
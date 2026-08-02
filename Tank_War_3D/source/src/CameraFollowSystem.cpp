#include "ECS/Systems/CameraFollowSystem.h"
#include "ECS/Core/World.h"
#include "ECS/Components/AllComponent.h"

void CameraFollowSystem::postUpdate(float deltaTime)
{
	auto& world = getWorld();

	auto entities = world.getEntitiesWith<Transform, CameraComponent, CameraFollow>();
	for (auto entity : entities)
	{
		auto& cameraFollow = entity.getComponent<CameraFollow>();
		if (!cameraFollow.target || !cameraFollow.target.hasComponent<Transform>())
			continue;
		processCameraFollow(entity, cameraFollow.target);
	}
}

void CameraFollowSystem::processCameraFollow(Entity& cameraEntity, Entity& targetEntity)
{
	auto& cameraTrans = cameraEntity.getComponent<Transform>();
	auto& cameraFollow = cameraEntity.getComponent<CameraFollow>();
	auto& cameraComponent = cameraEntity.getComponent<CameraComponent>();

	auto& trans = targetEntity.getComponent<Transform>();

	if (targetEntity.hasComponent<TagFreeCamera>())
	{
		processFreeCmaera(cameraEntity, targetEntity);
		return;
	}

	if (cameraComponent.isFirstPerson)
	{
		if (auto controller = targetEntity.tryGetComponent<Controller>())
		{
			cameraComponent.camera.SetYaw(controller->yaw);
			cameraComponent.camera.SetPitch(controller->pitch);
		}
		cameraComponent.SetPosition(trans.position + trans.rotation * cameraFollow.offset);

		auto cameraDircetion = cameraComponent.camera.GetDirection();

		cameraTrans.position = cameraComponent.camera.GetPosition();
		cameraTrans.rotation = glm::quatLookAt(-glm::normalize(glm::vec3(cameraDircetion.x, cameraDircetion.y, cameraDircetion.z)), glm::vec3(0.0f, 1.0f, 0.0f));

		trans.rotation = glm::quatLookAt(-glm::normalize(glm::vec3(cameraDircetion.x, 0.f, cameraDircetion.z)), glm::vec3(0.0f, 1.0f, 0.0f));
	}
}

void CameraFollowSystem::processFreeCmaera(Entity& cameraEntity, Entity& targetEntity)
{
	if (!targetEntity.hasComponent<Controller>())
		return;

	auto& cameraTrans = cameraEntity.getComponent<Transform>();
	auto& cameraComponent = cameraEntity.getComponent<CameraComponent>();

	auto& trans = targetEntity.getComponent<Transform>();
	auto& contoller = targetEntity.getComponent<Controller>();

	cameraComponent.camera.SetYaw(contoller.yaw);
	cameraComponent.camera.SetPitch(contoller.pitch);

	float velocity = std::max(0.05f, targetEntity.getComponent<TagFreeCamera>().velocity);
	if (contoller.getWantToForWard())
		cameraComponent.camera.SetPosition(cameraComponent.camera.GetPosition() + cameraComponent.camera.GetDirection() * velocity);
	else if (contoller.getWantToBackWard())
		cameraComponent.camera.SetPosition(cameraComponent.camera.GetPosition() - cameraComponent.camera.GetDirection() * velocity);

	if (contoller.getWantToLeft())
		cameraComponent.camera.SetPosition(cameraComponent.camera.GetPosition() - cameraComponent.camera.GetDirectionRight() * velocity);
	else if (contoller.getWantToRight())
		cameraComponent.camera.SetPosition(cameraComponent.camera.GetPosition() + cameraComponent.camera.GetDirectionRight() * velocity);

	auto cameraDircetion = cameraComponent.camera.GetDirection();

	cameraTrans.position = cameraComponent.camera.GetPosition();
	cameraTrans.rotation = glm::quatLookAt(-glm::normalize(glm::vec3(cameraDircetion.x, cameraDircetion.y, cameraDircetion.z)), glm::vec3(0.0f, 1.0f, 0.0f));

	trans.position = cameraTrans.position;
	trans.rotation = cameraTrans.rotation;
}

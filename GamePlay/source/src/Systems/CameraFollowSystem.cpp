#include "Systems/CameraFollowSystem.h"
#include "ECSCore/World.h"
#include "CommonComponent.h"
#include "GamePlayComponents.h"

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
			CameraFrontData data(controller->yaw, controller->pitch, cameraComponent.worldUp);

			auto& cameraDircetion = data.cameraFront;

			cameraTrans.position = trans.position + trans.rotation * cameraFollow.offset;
			cameraTrans.rotation = glm::quatLookAt(-glm::normalize(glm::vec3(cameraDircetion.x, cameraDircetion.y, cameraDircetion.z)), glm::vec3(0.0f, 1.0f, 0.0f));

			trans.rotation = glm::quatLookAt(-glm::normalize(glm::vec3(cameraDircetion.x, 0.f, cameraDircetion.z)), glm::vec3(0.0f, 1.0f, 0.0f));
		}
	}
}

void CameraFollowSystem::processFreeCmaera(Entity& cameraEntity, Entity& targetEntity)
{
	if (!targetEntity.hasComponent<Controller>())
		return;

	auto& cameraTrans = cameraEntity.getComponent<Transform>();
	auto& cameraComponent = cameraEntity.getComponent<CameraComponent>();

	auto& trans = targetEntity.getComponent<Transform>();
	auto& controller = targetEntity.getComponent<Controller>();

	CameraFrontData data(controller.yaw, controller.pitch, cameraComponent.worldUp);

	float velocity = std::max(0.05f, targetEntity.getComponent<TagFreeCamera>().velocity);
	if (controller.getWantToForWard())
		cameraTrans.position += data.cameraFront * velocity;
	else if (controller.getWantToBackWard())
		cameraTrans.position -= data.cameraFront * velocity;
	if (controller.getWantToLeft())
		cameraTrans.position -= data.cameraRight * velocity;
	else if (controller.getWantToRight())
		cameraTrans.position += data.cameraRight * velocity;
	if (controller.getWantToUp())
		cameraTrans.position += data.cameraUp * velocity;
	else if (controller.getWantToDown())
		cameraTrans.position -= data.cameraUp * velocity;

	auto& cameraDircetion = data.cameraFront;
	cameraTrans.rotation = glm::quatLookAt(-glm::normalize(glm::vec3(cameraDircetion.x, cameraDircetion.y, cameraDircetion.z)), glm::vec3(0.0f, 1.0f, 0.0f));

	trans.position = cameraTrans.position;
	trans.rotation = cameraTrans.rotation;
}

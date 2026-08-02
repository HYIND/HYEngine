#include "ECS/Systems/MovementSystem.h"
#include "ECS/Core/World.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/Movement.h"

#define _USE_MATH_DEFINES
#include <math.h>

void handleCharacterMove(Transform& trans, Controller& controller, CharacterMovement& movement)
{
	glm::vec3 direction = glm::vec3(0.f);

	if (controller.getWantToForWard())
		direction.z += 1;
	else if (controller.getWantToBackWard())
		direction.z -= 1;

	if (controller.getWantToLeft())
		direction.x += 1;
	else if (controller.getWantToRight())
		direction.x -= 1;

	if (direction.x != 0 || direction.y != 0 || direction.z != 0)
		direction = glm::normalize(direction);

	movement.setCurrentMoveDirection(direction);
	movement.SetCurrentJump(controller.getWantToJump());
}

void MovementSystem::update(float deltaTime)
{
	auto& world = getWorld();
	std::vector<Entity> entities = world.getEntitiesWith<Transform, Controller, CharacterMovement>();
	for (auto& entity : entities)
	{
		auto& transform = world.getComponent<Transform>(entity);
		auto& movement = world.getComponent<CharacterMovement>(entity);
		auto& contoller = world.getComponent<Controller>(entity);
		handleCharacterMove(transform, contoller, movement);
	}
}

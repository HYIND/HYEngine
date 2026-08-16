#include "Factory/CharacterFactory.h"
#include "CommonComponent.h"
#include "GameRuntimeComponents.h"
#include "GamePlayComponents.h"

Entity CharacterFactory::CreatePlayerCharacter(World& world, const glm::vec3& position, const glm::quat& rotation, std::shared_ptr<Model> model)
{
	Entity character = world.createEntityWithTag<TagCharacter>();
	character.addComponent<TagPlayer>();

	auto& trans = character.addComponent<Transform>();
	trans.position = position;
	trans.rotation = rotation;

	character.addComponent<PlayerInput>();
	auto& controller = character.addComponent<Controller>();
	character.addComponent<Movement>();
	character.addComponent<Health>();

	auto& rendermodel = character.addComponent<RenderModel>();
	rendermodel.model = model;

	AABB aabb = rendermodel.model->GetAABB();
	rendermodel.trans = glm::translate(rendermodel.trans, glm::vec3(0, -((aabb.min.y + aabb.max.y) / 2), 0));

	float height = aabb.max.y - aabb.min.y;
	float radius = std::max(aabb.max.x - aabb.min.x, aabb.max.z - aabb.min.z) / 2.f;

	auto& physics = character.addComponent<Physics>();
	physics.bodyType = Physics::BodyType::Dynamic;
	physics.isSensor = false;
	physics.isBullet = true;

	physics.isCharacter = true;
	physics.walkSpeed = 10.f;
	physics.jumpSpeed = 5.f;

	physics.collisionShape.AddCapsuleShape(radius, std::max(0.f, height - radius * 2));

	return character;
}

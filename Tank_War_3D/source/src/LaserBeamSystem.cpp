#include "ECS/Systems/LaserBeamSystem.h"
#include "ECS/Systems/PhysicsSystem.h"
#include "ECS/Core/World.h"

LaserBeamSystem::LaserBeamSystem()
{
}

void LaserBeamSystem::update(float deltaTime)
{
	std::vector<Entity> entities = m_world->getEntitiesWith<LaserBeamEmitter, Transform>();
	for (auto& entity : entities)
		UpdateLaserBeamEmitter(entity, deltaTime);
}

void LaserBeamSystem::UpdateLaserBeamEmitter(Entity& entity, float deltaTime)
{
	if (!entity)
		return;

	auto& trans = entity.getComponent<Transform>();
	auto& emitter = entity.getComponent<LaserBeamEmitter>();

	float deltaSceond = deltaTime / 1000.f;

	if (!emitter.properties)
		return;

	emitter.properties->start = trans.position;

	constexpr float maxDistance = 500.f;
	glm::vec3 dir = glm::normalize(trans.rotation * glm::vec3(0, 0, -1));
	glm::vec3 maxEnd = trans.position + dir * maxDistance;

	auto physicsSystem = m_world->getSystem<PhysicsSystem>();
	if (!physicsSystem)
	{
		emitter.properties->end = maxEnd;
		return;
	}

	auto res = physicsSystem->raycast(trans.position, maxEnd);
	if (!res.hit)
	{
		emitter.properties->end = maxEnd;
		return;
	}

	emitter.properties->end = trans.position + dir * res.distance;
}

#include "ECS/Systems/MapBoundarySystem.h"
#include "ECS/Components/AllComponent.h"
#include "ECS/Core/World.h"

static btVector3 GlmToBullet(const glm::vec3& v) {
	return btVector3(v.x, v.y, v.z);
}

static btQuaternion GlmToBullet(const glm::quat& q) {
	return btQuaternion(q.x, q.y, q.z, q.w);
}

void MapBoundarySystem::SetBoundaryHeight(float height)
{
	boundaryHeight = height;
}

void MapBoundarySystem::update(float deltaTime)
{
	auto entities = m_world->getEntitiesWith<TagCharacter>();
	for (Entity& entity : entities)
	{
		if (!entity.hasComponent<Transform>())
			continue;

		auto& trans = entity.getComponent<Transform>();
		if (trans.position.y <= boundaryHeight)
		{
			trans.position = glm::vec3(5, 6, 100);
			trans.rotation = glm::identity<glm::quat>();
		}

		if (!entity.hasComponent<Physics>())
			continue;

		auto& physics = entity.getComponent<Physics>();
		if (physics.ghostObject)
		{
			btTransform btTrans;
			btTrans.setOrigin(GlmToBullet(trans.position));
			btTrans.setRotation(GlmToBullet(trans.rotation));
			physics.ghostObject->setWorldTransform(btTrans);
		}
	}
}

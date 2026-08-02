#include "ECS/Systems/HealthSystem.h"
#include "ECS/Components/AllComponent.h"
#include "ECS/Core/World.h"
#include "ECS/Event/ECSEventDef.h"

void HealthSystem::onAttach(World& world)
{
	m_world->Subscribe<HealthSystem, DamageEvent>(
		[&](const DamageEvent& event)-> void
		{
			auto source = event.source;
			auto target = event.target;

			processDamageEvent(source, target, event.damage);
		}
	);
}

void HealthSystem::processDamageEvent(Entity source, Entity target, int damage)
{
	if (auto* health = target.tryGetComponent<Health>())
	{
		if (health->isInvulnerable)
			return;

		health->takeDamage(damage);
		m_world->Emit(HealthChangedEvent{
					.entity = target,
					.currentHealth = health->currentHealth,
					.maxHealth = health->maxHealth
			});

		if (!health->isAlive())
		{
			EntityDeathEvent event{
				.entity = target,
				.cause = EntityDeathEvent::DeathCause::HealthDepleted,
				.killer = source
			};
			if (auto* trans = target.tryGetComponent<Transform>())
				event.deathPosition = trans->position;
			m_world->Emit<EntityDeathEvent>(event);
		}
	}
}


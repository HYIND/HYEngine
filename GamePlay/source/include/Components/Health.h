#pragma once

#include <iostream>
#include "ECSCore/IComponent.h"

struct Health :public IComponent
{
	int currentHealth = 100;
	int maxHealth = 100;
	bool isInvulnerable = false;

	Health() = default;

	Health(int maxHealth)
		:currentHealth(maxHealth), maxHealth(maxHealth)
	{
	}

	void takeDamage(int damage)
	{
		damage = std::max(0, damage);

		if (!isInvulnerable) {
			int newcurenthealth = currentHealth - damage;
			currentHealth = std::max(0, newcurenthealth);
		}
	}

	void heal(int amount) {

		amount = std::max(0, amount);


		int newcurenthealth = currentHealth + amount;
		currentHealth = std::min(newcurenthealth, maxHealth);
	}

	bool isAlive() const {
		return currentHealth > 0;
	}

	float getHealthPercentage() const {
		return static_cast<float>(currentHealth) / maxHealth;
	}
};

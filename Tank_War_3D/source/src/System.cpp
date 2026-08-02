#include "ECS/Core/System.h"
#include "ECS/Core/World.h"

void System::Attach(World& world) {
	m_world = &world;
	onAttach(world);
}

void System::Detach() {
	m_world = nullptr;
	onDetach();
}

void System::setName(const std::string& name)
{
	m_name = name;
}

const std::string& System::getName() const
{
	return m_name;
}

bool System::isEnabled() const
{
	if (!m_world)
		throw("System not attached to a World!");
	return m_world->isSystemEnabled(m_name);
}

int System::getPriority() const
{
	return m_world->getSystemPriority(m_name);
}

World& System::getWorld() const {
	assert(m_world && "System not attached to a World!");
	return *m_world;
}

void System::setWorld(World* world)
{
	m_world = world;
}

void System::setEnabled(bool enabled)
{
	if (!m_world)
		throw("System not attached to a World!");
	m_world->setSystemEnabled(m_name, enabled);
}

void System::setPriority(int priority)
{
	if (!m_world)
		throw("System not attached to a World!");
	m_world->setSystemPriority(m_name, priority);
}

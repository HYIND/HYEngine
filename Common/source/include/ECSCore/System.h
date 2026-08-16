#pragma once

#include <iostream>
#include <assert.h>
#include <functional>

class World;

class System {

public:
	System(const std::string& name = "") : m_name(name) {}
	virtual ~System() = default;

	void Attach(World& world);
	void Detach();

	virtual void onAttach(World& world) {}
	virtual void onDetach() {}

	virtual void preUpdate(float deltaTime) {};   // 预更新（在update之前）
	virtual void update(float deltaTime) {};
	virtual void postUpdate(float deltaTime) {};  // 后更新（在update之后）

	virtual void fixedUpdate(float fixedDeltaTime) {}; // 固定时间步长更新


	void setName(const std::string& name);
	const std::string& getName() const;

	bool isEnabled() const;
	int getPriority() const;

	World& getWorld() const;

	void setWorld(World* world);
	void setEnabled(bool enabled);
	void setPriority(int priority);

protected:
	World* m_world = nullptr;
	std::string m_name;
};
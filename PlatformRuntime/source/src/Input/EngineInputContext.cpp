#include "Input/EngineInputContext.h"

void EngineInputContext::SetKeyDown(EngineKey key, bool down)
{
	if (!IsEngineKeyValid(key))
		return;

	std::lock_guard lock(m_mutex);
	const size_t index = EngineKeyIndex(key);
	if (down)
	{
		// 只在真正从抬起到按下时锁存按下边沿：按住不放的自动重复不会再触发一次。
		if (!m_keysDown.test(index))
			m_pressedLatch.set(index);
		m_keysDown.set(index);
	}
	else
	{
		if (m_keysDown.test(index))
			m_releasedLatch.set(index);
		m_keysDown.reset(index);
	}
}

void EngineInputContext::ReleaseAllKeys()
{
    LockGuard lock(m_mutex);
	m_keysDown.reset();
	m_pressedLatch.reset();
	m_releasedLatch.reset();
}

void EngineInputContext::NewFrame()
{
	LockGuard lock(m_mutex);
	m_frame.keysDown = m_keysDown;
	m_frame.keysPressed = m_pressedLatch;
	m_frame.keysReleased = m_releasedLatch;
	m_pressedLatch.reset();
	m_releasedLatch.reset();
}

bool EngineInputContext::IsKeyDown(EngineKey key) const
{
	if (!IsEngineKeyValid(key))
		return false;

	std::lock_guard lock(m_mutex);
	return m_keysDown.test(EngineKeyIndex(key));
}

EngineInputFrame EngineInputContext::ReadFrame() const
{
	std::lock_guard lock(m_mutex);
	return m_frame;
}

EngineInputFrame EngineInputContext::ConsumeFrame()
{
	std::lock_guard lock(m_mutex);
	EngineInputFrame frame = m_frame;
	m_frame.mouseDeltaX = 0.0f;
	m_frame.mouseDeltaY = 0.0f;
	return frame;
}

void EngineInputContext::SetActive(bool active)
{
    LockGuard lock(m_mutex);
	if (m_frame.active == active)
		return;

	m_frame.active = active;
	m_keysDown.reset();
	m_pressedLatch.reset();
	m_releasedLatch.reset();
	m_frame.keysDown.reset();
	m_frame.keysPressed.reset();
	m_frame.keysReleased.reset();
	m_frame.mouseDeltaX = 0.0f;
	m_frame.mouseDeltaY = 0.0f;
}

void EngineInputContext::AddMouseDelta(float deltaX, float deltaY)
{
    LockGuard lock(m_mutex);
    if (m_frame.active)
	{
		m_frame.mouseDeltaX += deltaX;
		m_frame.mouseDeltaY += deltaY;
	}
}

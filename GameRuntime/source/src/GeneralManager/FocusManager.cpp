#include "GeneralManager/FocusManager.h"

FocusManager* FocusManager::Instance()
{
	static FocusManager* m_instance = new FocusManager();
	return m_instance;
}

void FocusManager::SetFocus(bool enabled) { m_hasFocus = enabled; }

void FocusManager::SetActive(bool enabled) { m_isActive = enabled; }

bool FocusManager::HasFocus() const { return m_hasFocus; }

bool FocusManager::IsActive() const { return m_isActive; }

bool FocusManager::ShouldProcessInput() const
{
	return m_hasFocus && m_isActive;
}

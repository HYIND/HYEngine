#include "Manager/FocusManager.h"

FocusManager* FocusManager::Instance()
{
	static FocusManager* m_instance = new FocusManager();
	return m_instance;
}
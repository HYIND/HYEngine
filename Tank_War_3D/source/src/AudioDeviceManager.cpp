#include "Manager/AudioDeviceManager.h"

AudioDeviceManager* AudioDeviceManager::Instance()
{
	static AudioDeviceManager* m_instance = new AudioDeviceManager();
	return m_instance;
}

std::shared_ptr<AudioDevice>& AudioDeviceManager::GetDevice()
{
	return _device;
}

AudioDeviceManager::AudioDeviceManager()
{
	_device = std::make_shared<AudioDevice>();
}
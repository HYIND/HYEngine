#pragma once

#include "AudioProc/AudioTool.h"

enum class AudioChannelDef :AudioChannelID
{
	BGM_Channel = 0,
	SoundEffects_Channel = 1
};

class AudioDeviceManager {
public:
	static AudioDeviceManager* Instance();
	std::shared_ptr<AudioDevice>& GetDevice();
private:
	AudioDeviceManager();
	~AudioDeviceManager();

private:
	std::shared_ptr<AudioDevice> _device;
};

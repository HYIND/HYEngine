#pragma once

#include "ECSCore/System.h"
#include "GeneralManager/AudioDeviceManager.h"
#include "Audio/Components/AudioSource.h"

class AudioSystem :public System
{
public:
	virtual void onAttach(World& world) override;
	virtual void onDetach() override;

	virtual void postUpdate(float deltaTime);

private:
	void UpdateAudioSource(std::shared_ptr<AudioDevice>& audioDevice, Entity& entity, AudioSource& audioSource);
	void StopAudio(std::shared_ptr<AudioDevice>& audioDevice, AudioSource& audioSource);
	void PlayAudio(std::shared_ptr<AudioDevice>& audioDevice, Entity& entity, AudioSource& audioSource);
};
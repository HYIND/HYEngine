#pragma once

#include "ECSCore/System.h"
#include "Helper/AudioTool.h"
#include "Audio/Components/AudioSource.h"

class AudioSystem :public System
{
public:
	explicit AudioSystem(std::shared_ptr<AudioDevice> audioDevice);
	virtual void onAttach(World& world) override;
	virtual void onDetach() override;

	virtual void postUpdate(float deltaTime);

private:
	void UpdateAudioSource(std::shared_ptr<AudioDevice>& audioDevice, Entity& entity, AudioSource& audioSource);
	void StopAudio(std::shared_ptr<AudioDevice>& audioDevice, AudioSource& audioSource);
	void PlayAudio(std::shared_ptr<AudioDevice>& audioDevice, Entity& entity, AudioSource& audioSource);
	std::shared_ptr<AudioDevice> m_audioDevice;
};

HY_DECLARE_ECS_TYPE(AudioSystem, "hy.engine.system.Audio");

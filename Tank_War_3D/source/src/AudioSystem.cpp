#include "ECS/Systems/AudioSystem.h"
#include "ECS/Core/World.h"
#include "Manager/AudioDeviceManager.h"
#include "Manager/ResourceManager.h"

void AudioSystem::onAttach(World& world)
{
}

void AudioSystem::onDetach()
{
}

void AudioSystem::spawnAudio(const std::string& resname)
{
	if (auto audio = ResFactory->GetAudioRes(resname))
	{
		if (auto device = AudioDeviceManager::Instance()->GetDevice())
			device->PlayAudio(*audio, nullptr, (AudioChannelID)AudioChannelDef::SoundEffects_Channel);
	}
}

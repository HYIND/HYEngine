#include "Audio/Systems/AudioSystem.h"
#include "ECSCore/World.h"
#include "Audio/Components/AudioSource.h"
#include "Components/Transform.h"
#include "Helper/Tools.h"

static X3DAUDIO_VECTOR GLMToX3DVector(const glm::vec3& vec)
{
	return X3DAUDIO_VECTOR(vec.x, vec.y, -vec.z);
}

static glm::vec3 GetUpDir(const glm::vec3& dir)
{
	if (glm::abs(dir.y) > 0.9999f)
		return (dir.y > 0) ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
	return glm::vec3(0.0f, 1.0f, 0.0f);
}

static Audio3DParams Get3DParams(Entity& entity, AudioSource& audioSource)
{
	Audio3DParams params;
	if (!audioSource.is3D)
	{
		params.is3D = false;
		return params;
	}

	auto* trans = entity.tryGetComponent<Transform>();
	if (!trans)
	{
		params.is3D = false;
		return params;
	}

	glm::vec3 dir = Tool::GetDirectionFromRotate(trans->rotation);
	glm::vec3 up = GetUpDir(dir);
	params.position = GLMToX3DVector(trans->position);
	params.forward = GLMToX3DVector(dir);
	params.up = GLMToX3DVector(up);
	params.velocity = GLMToX3DVector(glm::vec3(0.f));

	params.minDistance = audioSource.minDistance;
	params.maxDistance = audioSource.maxDistance;
	params.rolloffFactor = audioSource.rolloffFactor;

	params.innerVolume = audioSource.volume * audioSource.innerVolume;
	params.innerAngle = Tool::AngleToRadian(audioSource.innerAngle);
	params.innerLPF = audioSource.innerLPF;
	params.innerReverb = audioSource.innerReverb;
	params.outerVolume = audioSource.volume * audioSource.outerVolume;
	params.outerAngle = Tool::AngleToRadian(audioSource.outerAngle);
	params.outerLPF = audioSource.outerLPF;
	params.outerReverb = audioSource.outerReverb;

	params.occlusionLPF = audioSource.occlusionLPF;

	params.is3D = true;

	return params;
}

void AudioSystem::onAttach(World& world)
{
	m_world->Subscribe<AudioSystem, EntityDestroyedEvent>(
		[&](const EntityDestroyedEvent& event)-> void
		{
			if (event.entity.hasComponent<AudioSource>())
			{
				if (auto device = AudioDeviceManager::Instance()->GetDevice())
				{
					auto& audioSource = event.entity.getComponent<AudioSource>();
					StopAudio(device, audioSource);
				}
			}
		}
	);
}

void AudioSystem::onDetach()
{
}

void AudioSystem::postUpdate(float deltaTime)
{
	auto device = AudioDeviceManager::Instance()->GetDevice();
	if (!device)
		return;

	auto entities = m_world->getViewWith<AudioSource>();

	if (m_world->HasMainCameraEntity())
	{
		auto camera = m_world->GetMainCameraEntity();
		if (auto* trans = camera.tryGetComponent<Transform>())
		{
			glm::vec3 dir = Tool::GetDirectionFromRotate(trans->rotation);
			glm::vec3 up = GetUpDir(dir);

			AudioListener listener;
			listener.position = GLMToX3DVector(trans->position);
			listener.forward = GLMToX3DVector(dir);
			listener.up = GLMToX3DVector(up);
			listener.velocity = GLMToX3DVector(glm::vec3(0.f));

			device->SetListener(listener);
		}
	}

	for (auto [entity, audioSource] : entities)
		UpdateAudioSource(device, entity, audioSource);

	device->Update3DAudio();
}

void AudioSystem::UpdateAudioSource(std::shared_ptr<AudioDevice>& audioDevice, Entity& entity, AudioSource& audioSource)
{
	if (!audioSource.is3D)
		return;

	// ===== 处理播放状态变化 =====
	if (audioSource.status->needsUpdate) {
		switch (audioSource.status->state)
		{
		case AudioState::Playing:
		{
			if (!audioSource.status->playHandle)
				PlayAudio(audioDevice, entity, audioSource);
			break;
		}
		case AudioState::Stopped:
		{
			if (audioSource.status->playHandle)
				StopAudio(audioDevice, audioSource);
			break;
		}
		case AudioState::Complete:
		{
			if (audioSource.isLooping)
				PlayAudio(audioDevice, entity, audioSource);
			break;
		}
		break;
		}
		audioSource.status->needsUpdate = false;
	}

	// ===== 2. 计算3D参数 (如果正在播放) =====
	if (audioSource.is3D && audioSource.status->state == AudioState::Playing && audioSource.status->playHandle)
	{
		Audio3DParams params = Get3DParams(entity, audioSource);
		audioDevice->SetSource3DParams(audioSource.status->playHandle, params);
	}
}

void AudioSystem::StopAudio(std::shared_ptr<AudioDevice>& audioDevice, AudioSource& audioSource)
{
	audioDevice->StopAudio(audioSource.status->playHandle);
	audioSource.status->playHandle = AudioPlayHandle();
}

void AudioSystem::PlayAudio(std::shared_ptr<AudioDevice>& audioDevice, Entity& entity, AudioSource& audioSource)
{
	Audio3DParams params = Get3DParams(entity, audioSource);
	audioSource.status->playHandle = audioDevice->PlayAudio(*(audioSource.info), audioSource.channel, params, [status = audioSource.status](AudioPlayHandle handle)->void {
		status->playHandle = AudioPlayHandle();
		status->state = AudioState::Complete;
		status->needsUpdate = true;
		});
}


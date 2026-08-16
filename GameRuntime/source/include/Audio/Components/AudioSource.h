#pragma once

#include "ECSCore/IComponent.h"
#include "GeneralManager/AudioDeviceManager.h"

enum class AudioState {
	Stopped,    // 停止
	Playing,    // 播放中
	Complete	// 已完成
};

struct AudioStatusProxy {
	AudioState state = AudioState::Stopped;
	AudioPlayHandle playHandle;
	bool needsUpdate = false;
};

struct AudioSource : public IComponent
{
	// ========== 音频属性 ==========
	std::shared_ptr<AudioInfo> info;
	bool isLooping = false;
	AudioChannelID channel = (AudioChannelID)AudioChannelDef::SoundEffects_Channel;

	float volume = 1.0f;
	float pitch = 1.0f;
	bool is3D = true;

	// ========== 3D参数 ==========
	float minDistance = 1.0f;
	float maxDistance = 100.f;     // 用于距离曲线缩放
	float rolloffFactor = 1.0f;

	// 方向锥
	float innerAngle = 360.f;   // 360度 = 全向
	float innerVolume = 1.0f;	// 内锥音量
	float innerLPF = 0.0f;		// 内锥低通滤波，使声音变沉闷
	float innerReverb = 0.708f;	// 内锥混响
	float outerAngle = 360.f;
	float outerVolume = 0.708f;
	float outerLPF = 0.25f;
	float outerReverb = 1.0f;

	// ========== 遮挡 ==========
	float occlusionLPF = 1.0f;      // 1.0=无遮挡, 0.0=完全遮挡

	// ========== 音频状态 ==========
	std::shared_ptr<AudioStatusProxy> status = std::make_shared<AudioStatusProxy>();

	AudioSource() {}
	AudioSource(std::shared_ptr<AudioInfo> audioinfo) :info(audioinfo) {}
	void Play() { status->state = AudioState::Playing; status->needsUpdate = true; }
	void Stop() { status->state = AudioState::Stopped; status->needsUpdate = true; }
	bool IsPlaying() const { return status->state == AudioState::Playing; }
};
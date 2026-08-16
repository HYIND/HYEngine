#pragma once

#include <iostream>
#define XAUDIO2_HELPER_FUNCTIONS
#include <xaudio2.h>
#pragma comment(lib, "xaudio2.lib")

#include <vector>
#include "SafeStl.h"
#include "Coroutine.h"
#include <x3daudio.h>


struct Audio3DParams
{
	// 位置/速度
	X3DAUDIO_VECTOR position;
	X3DAUDIO_VECTOR forward;
	X3DAUDIO_VECTOR up;
	X3DAUDIO_VECTOR velocity;

	// 衰减参数
	float minDistance = 1.0f;
	float maxDistance = 100.0f;
	float rolloffFactor = 1.0f;

	// 方向锥
	float innerAngle = X3DAUDIO_2PI;   // 360度 = 全向
	float innerVolume = 1.0f;
	float innerLPF = 0.0f;
	float innerReverb = 0.708f;
	float outerAngle = X3DAUDIO_2PI;
	float outerVolume = 0.708f;
	float outerLPF = 0.25f;
	float outerReverb = 1.0f;

	float occlusionLPF = 1.0f;

	// 是否启用3D
	bool is3D = false;
};

struct AudioListener {
	X3DAUDIO_VECTOR position;
	X3DAUDIO_VECTOR forward;   // 朝向
	X3DAUDIO_VECTOR up;        // 上方向
	X3DAUDIO_VECTOR velocity;
	float dopplerFactor = 1.0f;
};

class AudioInfo;

using SourceVoiceID = std::string;
using AudioChannelID = uint32_t;

class AudioPlayHandle
{
public:
	AudioPlayHandle();
	static AudioPlayHandle Create();

	explicit operator bool() const;
	bool operator==(const AudioPlayHandle& other) const;
	bool operator!=(const AudioPlayHandle& other) const;
	const SourceVoiceID& getId() const;
private:
	SourceVoiceID id;
};
namespace std {
	template<> struct hash<AudioPlayHandle> {
		size_t operator()(const AudioPlayHandle& handle) const {
			return hash<SourceVoiceID>()(handle.getId());
		}
	};
}

enum class DeviceState {
	Normal,
	Recovering,
	Failed
};

struct SourceVoiceHandle;
class AudioDevice :public IXAudio2VoiceCallback
{
public:
	AudioDevice();
	~AudioDevice();

	AudioPlayHandle PlayAudio(const AudioInfo& audio, AudioChannelID = 0, Audio3DParams audio3DParams = {}, std::function<void(AudioPlayHandle)> complateCallback = nullptr);
	bool StopAudio(const AudioPlayHandle& handle);
	bool IsPlaying(const AudioPlayHandle& handle) const;

	bool SetMasterVolumn(float volumn);	//0.f-1.f;

	bool AddChannel(AudioChannelID id);
	bool SetChannelVolumn(AudioChannelID id, float volumn);	//0.f-1.f;

	// 新增3D接口
	void SetListener(const AudioListener& listener);
	void SetSource3DParams(const AudioPlayHandle& handle, const Audio3DParams& params);
	void Update3DAudio();  // 每帧调用，更新所有3D音源

private:
	virtual void OnBufferEnd(void*) override;
	virtual void OnVoiceError(void*, HRESULT) override;

	virtual void OnVoiceProcessingPassStart(UINT32) override {}
	virtual void OnVoiceProcessingPassEnd() override {}
	virtual void OnBufferStart(void*) override {}
	virtual void OnLoopEnd(void*) override {}
	virtual void OnStreamEnd() {};

	bool InitVoiceDevice();
	void RestoreVoiceDevice();

	void CleanUp();

	void Calculate3DEffect(std::shared_ptr<SourceVoiceHandle>& sourceVoiceHandle, IXAudio2SubmixVoice* targetVoice);

private:
	IXAudio2* m_xAudio2;
	IXAudio2MasteringVoice* m_pXAudio2MasteringVoice;
	std::atomic<DeviceState> m_deviceState;
	SafeUnorderedMap<AudioPlayHandle, std::shared_ptr<SourceVoiceHandle>, CoroCriticalSectionLock> m_HandleToSourceVoiceHandle;
	SafeUnorderedMap<AudioChannelID, IXAudio2SubmixVoice*, CoroCriticalSectionLock> m_ChannelIDToSubmixVoice;

	X3DAUDIO_HANDLE _x3DInstance;
	DWORD _channelMask = 0;
	AudioListener _listener;
	bool _listenerDirty = true;
};

class AudioInfo
{
public:
	AudioInfo();
	~AudioInfo();

	bool LoadAudioFromFile(const std::string& filepath);
	bool LoadAudioFromResource(HINSTANCE hinstance, LPCWSTR resourceType, LPCWSTR resourceName);

	void Cleanup();
	bool Valid() const;

private:
	WAVEFORMATEXTENSIBLE _wfx = { 0 };
	XAUDIO2_BUFFER _buffer;

	bool _valid;

	friend class AudioDevice;
};

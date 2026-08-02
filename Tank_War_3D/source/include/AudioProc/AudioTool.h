#pragma once

#include <iostream>
#include <xaudio2.h>
#include <vector>
#include "SafeStl.h"
#include "Coroutine.h"

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

	AudioPlayHandle PlayAudio(const AudioInfo& audio, std::function<void(AudioPlayHandle)> complateCallback = nullptr, AudioChannelID = 0);

	bool StopAudio(const AudioPlayHandle& handle);

	bool SetMasterVolumn(float volumn);	//0.f-1.f;

	bool AddChannel(AudioChannelID id);
	bool SetChannelVolumn(AudioChannelID id, float volumn);	//0.f-1.f;

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
private:
	IXAudio2* m_xAudio2;
	IXAudio2MasteringVoice* m_pXAudio2MasteringVoice;
	std::atomic<DeviceState> m_deviceState;
	SafeUnorderedMap<AudioPlayHandle, std::shared_ptr<SourceVoiceHandle>, CoroCriticalSectionLock> m_HandleToSourceVoiceHandle;
	SafeUnorderedMap<AudioChannelID, IXAudio2SubmixVoice*, CoroCriticalSectionLock> m_ChannelIDToSubmixVoice;
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

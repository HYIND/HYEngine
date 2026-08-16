#include "Helper/AudioTool.h"
#include "Net/Helper/Buffer.h"
#include "Helper/Tools.h"
#include <algorithm>

#define fourccRIFF 'FFIR'
#define fourccDATA 'atad'
#define fourccFMT ' tmf'
#define fourccWAVE 'EVAW'
#define fourccXWMA 'AMWX'
#define fourccDPDS 'sdpd'

HRESULT FindChunk(Buffer& buf, DWORD fourcc, DWORD& dwChunkSize, DWORD& dwChunkDataPosition)
{
	HRESULT hr = S_OK;
	if (buf.Length() == 0)
		return HRESULT_FROM_WIN32(GetLastError());

	buf.Seek(0);

	DWORD dwChunkType;
	DWORD dwChunkDataSize;
	DWORD dwRIFFDataSize = 0;
	DWORD dwFileType;
	DWORD dwOffset = 0;

	while (hr == S_OK)
	{
		if (buf.Read(&dwChunkType, sizeof(DWORD)) != sizeof(DWORD))
			hr = HRESULT_FROM_WIN32(GetLastError());

		if (buf.Read(&dwChunkDataSize, sizeof(DWORD)) != sizeof(DWORD))
			hr = HRESULT_FROM_WIN32(GetLastError());

		switch (dwChunkType)
		{
		case fourccRIFF:
			dwRIFFDataSize = dwChunkDataSize;
			dwChunkDataSize = 4;
			if (buf.Read(&dwFileType, sizeof(DWORD)) != sizeof(DWORD))
				hr = HRESULT_FROM_WIN32(GetLastError());
			break;

		default:
			uint64_t goal_pos = buf.Position() + dwChunkDataSize;
			if (goal_pos != buf.Seek(goal_pos))
				return HRESULT_FROM_WIN32(GetLastError());
		}

		dwOffset += sizeof(DWORD) * 2;

		if (dwChunkType == fourcc)
		{
			dwChunkSize = dwChunkDataSize;
			dwChunkDataPosition = dwOffset;
			return S_OK;
		}

		dwOffset += dwChunkDataSize;
	}

	return S_OK;
}

HRESULT ReadChunkData(Buffer& buf, void* buffer, DWORD buffersize, DWORD bufferoffset)
{
	HRESULT hr = S_OK;
	if (bufferoffset != buf.Seek(bufferoffset))
		return HRESULT_FROM_WIN32(GetLastError());
	if (buf.Read(buffer, buffersize) != buffersize)
		hr = HRESULT_FROM_WIN32(GetLastError());
	return hr;
}

void GetChannelName(DWORD channelMask, std::vector<std::string>& channelName)
{
	channelName.clear();
	if (channelMask & SPEAKER_FRONT_LEFT) channelName.push_back("SPEAKER_FRONT_LEFT");
	if (channelMask & SPEAKER_FRONT_RIGHT)channelName.push_back("SPEAKER_FRONT_RIGHT");
	if (channelMask & SPEAKER_FRONT_CENTER) channelName.push_back("SPEAKER_FRONT_CENTER");
	if (channelMask & SPEAKER_LOW_FREQUENCY) channelName.push_back("SPEAKER_LOW_FREQUENCY");
	if (channelMask & SPEAKER_BACK_LEFT) channelName.push_back("SPEAKER_BACK_LEFT");
	if (channelMask & SPEAKER_BACK_RIGHT) channelName.push_back("SPEAKER_BACK_RIGHT");
	if (channelMask & SPEAKER_FRONT_LEFT_OF_CENTER) channelName.push_back("SPEAKER_FRONT_LEFT_OF_CENTER");
	if (channelMask & SPEAKER_FRONT_RIGHT_OF_CENTER) channelName.push_back("SPEAKER_FRONT_RIGHT_OF_CENTER");
	if (channelMask & SPEAKER_BACK_CENTER)channelName.push_back("SPEAKER_BACK_CENTER");
	if (channelMask & SPEAKER_SIDE_LEFT) channelName.push_back("SPEAKER_SIDE_LEFT");
	if (channelMask & SPEAKER_SIDE_RIGHT)channelName.push_back("SPEAKER_SIDE_RIGHT");
	if (channelMask & SPEAKER_TOP_CENTER) channelName.push_back("SPEAKER_TOP_CENTER");
	if (channelMask & SPEAKER_TOP_FRONT_LEFT)channelName.push_back("SPEAKER_TOP_FRONT_LEFT");
	if (channelMask & SPEAKER_TOP_FRONT_CENTER) channelName.push_back("SPEAKER_TOP_FRONT_CENTER");
	if (channelMask & SPEAKER_TOP_FRONT_RIGHT) channelName.push_back("SPEAKER_TOP_FRONT_RIGHT");
	if (channelMask & SPEAKER_TOP_BACK_LEFT) channelName.push_back("SPEAKER_TOP_BACK_LEFT");
	if (channelMask & SPEAKER_TOP_BACK_CENTER) channelName.push_back("SPEAKER_TOP_BACK_CENTER");
	if (channelMask & SPEAKER_TOP_BACK_RIGHT) channelName.push_back("SPEAKER_TOP_BACK_RIGHT");
}

void GetChannelCount(DWORD channelMask, UINT32& channelCount)
{
	UINT32 dstChannelCount = 0;
	if (channelMask & SPEAKER_FRONT_LEFT) dstChannelCount++;
	if (channelMask & SPEAKER_FRONT_RIGHT) dstChannelCount++;
	if (channelMask & SPEAKER_FRONT_CENTER) dstChannelCount++;
	if (channelMask & SPEAKER_LOW_FREQUENCY) dstChannelCount++;
	if (channelMask & SPEAKER_BACK_LEFT) dstChannelCount++;
	if (channelMask & SPEAKER_BACK_RIGHT) dstChannelCount++;
	if (channelMask & SPEAKER_FRONT_LEFT_OF_CENTER) dstChannelCount++;
	if (channelMask & SPEAKER_FRONT_RIGHT_OF_CENTER) dstChannelCount++;
	if (channelMask & SPEAKER_BACK_CENTER) dstChannelCount++;
	if (channelMask & SPEAKER_SIDE_LEFT) dstChannelCount++;
	if (channelMask & SPEAKER_SIDE_RIGHT) dstChannelCount++;
	if (channelMask & SPEAKER_TOP_CENTER) dstChannelCount++;
	if (channelMask & SPEAKER_TOP_FRONT_LEFT) dstChannelCount++;
	if (channelMask & SPEAKER_TOP_FRONT_CENTER) dstChannelCount++;
	if (channelMask & SPEAKER_TOP_FRONT_RIGHT) dstChannelCount++;
	if (channelMask & SPEAKER_TOP_BACK_LEFT) dstChannelCount++;
	if (channelMask & SPEAKER_TOP_BACK_CENTER) dstChannelCount++;
	if (channelMask & SPEAKER_TOP_BACK_RIGHT) dstChannelCount++;

	channelCount = dstChannelCount;
}

void GetChannelAzimuthsFromWFX(WAVEFORMATEXTENSIBLE& wfx, UINT32& channel, std::vector<float>& azimuths)
{
	// 把角度转换为向前开始顺时针方向的方位角
	static auto AngleToAzimuth = [](float angle)->float
		{
			return fmod((fmod(angle, 360.f) + 360.f), 360.f) / 360.f * X3DAUDIO_PI;
		};


	channel = wfx.Format.nChannels;

	if (channel <= 1)
	{
		azimuths.resize(1, 0.0f);
		return;
	}

	// 先尝试从 WAVEFORMATEXTENSIBLE 获取 mask
	if (wfx.Format.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX) && wfx.dwChannelMask != 0)
	{
		azimuths.clear();
		if (wfx.dwChannelMask & SPEAKER_FRONT_LEFT) azimuths.push_back(AngleToAzimuth(-45.f));
		if (wfx.dwChannelMask & SPEAKER_FRONT_RIGHT) azimuths.push_back(AngleToAzimuth(45.f));
		if (wfx.dwChannelMask & SPEAKER_FRONT_CENTER) azimuths.push_back(0.f);
		if (wfx.dwChannelMask & SPEAKER_LOW_FREQUENCY) azimuths.push_back(X3DAUDIO_2PI);
		if (wfx.dwChannelMask & SPEAKER_BACK_LEFT) azimuths.push_back(-135.f);
		if (wfx.dwChannelMask & SPEAKER_BACK_RIGHT) azimuths.push_back(135.f);
		if (wfx.dwChannelMask & SPEAKER_FRONT_LEFT_OF_CENTER) azimuths.push_back(AngleToAzimuth(-22.5f));
		if (wfx.dwChannelMask & SPEAKER_FRONT_RIGHT_OF_CENTER) azimuths.push_back(AngleToAzimuth(22.5f));
		if (wfx.dwChannelMask & SPEAKER_BACK_CENTER)  azimuths.push_back(AngleToAzimuth(180.f));
		if (wfx.dwChannelMask & SPEAKER_SIDE_LEFT) azimuths.push_back(AngleToAzimuth(-90.f));
		if (wfx.dwChannelMask & SPEAKER_SIDE_RIGHT) azimuths.push_back(AngleToAzimuth(90.f));
		if (wfx.dwChannelMask & SPEAKER_TOP_CENTER) azimuths.push_back(AngleToAzimuth(0.f));
		if (wfx.dwChannelMask & SPEAKER_TOP_FRONT_LEFT) azimuths.push_back(AngleToAzimuth(-45.f));
		if (wfx.dwChannelMask & SPEAKER_TOP_FRONT_CENTER) azimuths.push_back(AngleToAzimuth(0.f));
		if (wfx.dwChannelMask & SPEAKER_TOP_FRONT_RIGHT) azimuths.push_back(AngleToAzimuth(45.f));
		if (wfx.dwChannelMask & SPEAKER_TOP_BACK_LEFT) azimuths.push_back(AngleToAzimuth(-135.f));
		if (wfx.dwChannelMask & SPEAKER_TOP_BACK_CENTER) azimuths.push_back(AngleToAzimuth(180.f));
		if (wfx.dwChannelMask & SPEAKER_TOP_BACK_RIGHT) azimuths.push_back(AngleToAzimuth(135.f));
	}
	else
	{
		switch (wfx.Format.nChannels)
		{
		case 1:
			azimuths = { 0.0f };
			break;
		case 2:
			//azimuths = { 0.0f, 0.0f }; // 退化为点声源
			azimuths = { AngleToAzimuth(-45.f), AngleToAzimuth(45.f) }; // 保留立体声
			break;
		case 6:
			// 5.1 的默认顺序：C, FL, FR, BL, BR, LFE
			azimuths = { 0.0f, AngleToAzimuth(-45.f), AngleToAzimuth(45.f),
						AngleToAzimuth(-135.f), AngleToAzimuth(135.f), X3DAUDIO_2PI };
			break;
		case 8:
			// 7.1 的默认顺序：C, FL, FR, SL, SR, BL, BR, LFE
			azimuths = { 0.0f, AngleToAzimuth(-45.f), AngleToAzimuth(45.f),
						AngleToAzimuth(-90.f), AngleToAzimuth(90.f),
						AngleToAzimuth(-135.f), AngleToAzimuth(135.f), X3DAUDIO_2PI };
			break;
		default:
			azimuths.resize(wfx.Format.nChannels, 0.0f);
			break;
		}
	}
}


struct SourceVoiceHandle
{
	IXAudio2SourceVoice* source = nullptr;
	AudioChannelID channelId;
	struct
	{
		WAVEFORMATEXTENSIBLE wfx;
		XAUDIO2_BUFFER buffer;
	}audioData;
	std::function<void(AudioPlayHandle)> onComplateFunc;

	Audio3DParams params;
};

AudioPlayHandle::AudioPlayHandle()
{
}

AudioPlayHandle AudioPlayHandle::Create()
{
	static std::atomic<int> counter{ 0 };
	AudioPlayHandle res;
	res.id = "__Voice_" + std::to_string(++counter);
	return res;
}

AudioPlayHandle::operator bool() const
{
	return !id.empty();
}

bool AudioPlayHandle::operator==(const AudioPlayHandle& other) const
{
	return id == other.id;
}

bool AudioPlayHandle::operator!=(const AudioPlayHandle& other) const
{
	return id != other.id;
}
const SourceVoiceID& AudioPlayHandle::getId() const
{
	return id;
}

AudioInfo::AudioInfo()
	:_wfx{ 0 }, _buffer{}, _valid(false)
{
}

AudioInfo::~AudioInfo()
{
	Cleanup();
}

bool AudioInfo::LoadAudioFromFile(const std::string& filepath)
{
	Cleanup();

	std::wstring w_filepath = Tool::UTF8ToWString(filepath);

	// Open the file
	HANDLE hFile = CreateFile(
		w_filepath.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		0,
		NULL);

	if (INVALID_HANDLE_VALUE == hFile)
		return false;

	if (INVALID_SET_FILE_POINTER == SetFilePointer(hFile, 0, NULL, FILE_BEGIN))
		return false;

	DWORD dwFileSize = GetFileSize(hFile, NULL);
	if (dwFileSize == INVALID_FILE_SIZE) {
		CloseHandle(hFile);
		return false;
	}

	Buffer filebuffer(dwFileSize);
	HRESULT hr = S_OK;
	DWORD dwRead;
	if (0 == ReadFile(hFile, filebuffer.Data(), dwFileSize, &dwRead, NULL))
		hr = HRESULT_FROM_WIN32(GetLastError());
	CloseHandle(hFile);
	if (FAILED(hr))
		return false;

	DWORD dwChunkSize;
	DWORD dwChunkPosition;
	//check the file type, should be fourccWAVE or 'XWMA'
	FindChunk(filebuffer, fourccRIFF, dwChunkSize, dwChunkPosition);
	DWORD filetype;
	ReadChunkData(filebuffer, &filetype, sizeof(DWORD), dwChunkPosition);
	if (filetype != fourccWAVE)
		return false;

	FindChunk(filebuffer, fourccFMT, dwChunkSize, dwChunkPosition);
	ReadChunkData(filebuffer, &_wfx, dwChunkSize, dwChunkPosition);

	//fill out the audio data buffer with the contents of the fourccDATA chunk
	FindChunk(filebuffer, fourccDATA, dwChunkSize, dwChunkPosition);
	BYTE* pDataBuffer = new BYTE[dwChunkSize];
	ReadChunkData(filebuffer, pDataBuffer, dwChunkSize, dwChunkPosition);

	_buffer.AudioBytes = dwChunkSize;  //size of the audio buffer in bytes
	_buffer.pAudioData = pDataBuffer;  //buffer containing audio data
	_buffer.Flags = XAUDIO2_END_OF_STREAM; // tell the source voice not to expect any data after this buffer

	_valid = true;
	return true;
}

bool AudioInfo::LoadAudioFromResource(HINSTANCE hinstance, LPCWSTR resourceType, LPCWSTR resourceName)
{
	Cleanup();

	HRSRC hRes = FindResource((HMODULE)hinstance, resourceName, resourceType);
	if (!hRes) return false;

	// 加载资源
	HGLOBAL hData = LoadResource((HMODULE)hinstance, hRes);
	if (!hData) return false;

	// 获取资源大小和指针
	DWORD resourceSize = SizeofResource((HMODULE)hinstance, hRes);
	BYTE* pResourceData = (BYTE*)LockResource(hData);
	if (!pResourceData) return false;

	Buffer filebuffer(resourceSize);
	memcpy(filebuffer.Data(), pResourceData, resourceSize);

	DWORD dwChunkSize;
	DWORD dwChunkPosition;
	//check the file type, should be fourccWAVE or 'XWMA'
	FindChunk(filebuffer, fourccRIFF, dwChunkSize, dwChunkPosition);
	DWORD filetype;
	ReadChunkData(filebuffer, &filetype, sizeof(DWORD), dwChunkPosition);
	if (filetype != fourccWAVE)
		return false;

	FindChunk(filebuffer, fourccFMT, dwChunkSize, dwChunkPosition);
	ReadChunkData(filebuffer, &_wfx, dwChunkSize, dwChunkPosition);

	//fill out the audio data buffer with the contents of the fourccDATA chunk
	FindChunk(filebuffer, fourccDATA, dwChunkSize, dwChunkPosition);
	BYTE* pDataBuffer = new BYTE[dwChunkSize];
	ReadChunkData(filebuffer, pDataBuffer, dwChunkSize, dwChunkPosition);

	_buffer.AudioBytes = dwChunkSize;  //size of the audio buffer in bytes
	_buffer.pAudioData = pDataBuffer;  //buffer containing audio data
	_buffer.Flags = XAUDIO2_END_OF_STREAM; // tell the source voice not to expect any data after this buffer

	_valid = true;

	//printf("========== WAV 文件格式诊断 ==========\n");
	//printf("wFormatTag: %d %s\n", _wfx.Format.wFormatTag, 
	//	   _wfx.Format.wFormatTag == WAVE_FORMAT_PCM ? "(PCM)" :
	//	   _wfx.Format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT ? "(Float)" :
	//	   _wfx.Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE ? "(Extensible)" : "(Unknown)");
	//printf("nChannels: %d\n", _wfx.Format.nChannels);
	//printf("nSamplesPerSec: %d\n", _wfx.Format.nSamplesPerSec);
	//printf("wBitsPerSample: %d\n", _wfx.Format.wBitsPerSample);
	//printf("cbSize: %d\n", _wfx.Format.cbSize);
	//printf("dwChannelMask: 0x%08X\n", _wfx.dwChannelMask);

	//// 检查是否真的是 WAVEFORMATEXTENSIBLE
	//bool isExtensible = (_wfx.Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE) ||
	//					(_wfx.Format.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX));

	//printf("是否包含扩展信息: %s\n", isExtensible ? "是" : "否");
	//printf("======================================\n");

	return true;
}

void AudioInfo::Cleanup() {
	_valid = false;
	_wfx = { 0 };
	if (_buffer.pAudioData) {
		delete[] _buffer.pAudioData;
		_buffer.pAudioData = nullptr;
	}
	_buffer = XAUDIO2_BUFFER();
}

bool AudioInfo::Valid() const
{
	return _valid;
}

AudioDevice::AudioDevice()
	:m_xAudio2{}, m_pXAudio2MasteringVoice{}, m_deviceState{ DeviceState::Failed }
{
	InitVoiceDevice();
}

AudioDevice::~AudioDevice()
{
	CleanUp();
}

AudioPlayHandle AudioDevice::PlayAudio(const AudioInfo& audio, AudioChannelID channelId, Audio3DParams audio3DParams, std::function<void(AudioPlayHandle)> complateCallback)
{
	if (m_deviceState != DeviceState::Normal || !audio.Valid() || !m_xAudio2)
		return AudioPlayHandle();

	HRESULT hr = S_OK;

	if (!m_ChannelIDToSubmixVoice.Exist(channelId))
		AddChannel(channelId);

	//// 一个声音同时输出到多个目标
	//XAUDIO2_SEND_DESCRIPTOR sendDescs[] = {
	//	{0, m_pMusicSubmix},      // 输出到音乐组
	//	{0, m_pReverbSubmix},      // 也输出到混响组
	//	{XAUDIO2_SEND_USEFILTER, m_pMasterVoice} // 也直接输出到主设备（带滤波器）
	//};

	//XAUDIO2_VOICE_SENDS sendList = {
	//	3,              // 3个输出目标
	//	sendDescs       // 数组
	//};


	IXAudio2SubmixVoice* submitvoice = nullptr;
	if (!m_ChannelIDToSubmixVoice.Find(channelId, submitvoice) || !submitvoice)
		return AudioPlayHandle();

	XAUDIO2_SEND_DESCRIPTOR sendDesc = { 0, submitvoice };
	XAUDIO2_VOICE_SENDS sendList = { 1, &sendDesc };

	IXAudio2SourceVoice* voice;
	hr = m_xAudio2->CreateSourceVoice(
		&voice,
		(WAVEFORMATEX*)&audio._wfx,
		0,
		XAUDIO2_DEFAULT_FREQ_RATIO,
		this,
		&sendList,
		nullptr
	);
	if (FAILED(hr) || !voice)
		return AudioPlayHandle();

	AudioPlayHandle handle = AudioPlayHandle::Create();
	std::shared_ptr<SourceVoiceHandle> sourceVoiceHandle = std::make_shared<SourceVoiceHandle>();

	sourceVoiceHandle->source = voice;
	sourceVoiceHandle->channelId = channelId;

	sourceVoiceHandle->audioData.wfx = audio._wfx;
	sourceVoiceHandle->audioData.buffer.AudioBytes = audio._buffer.AudioBytes;
	sourceVoiceHandle->audioData.buffer.pAudioData = audio._buffer.pAudioData;  // 音频数据共享（只读）
	sourceVoiceHandle->audioData.buffer.Flags = audio._buffer.Flags;
	sourceVoiceHandle->audioData.buffer.pContext = new AudioPlayHandle(handle);

	sourceVoiceHandle->onComplateFunc = complateCallback;

	sourceVoiceHandle->params = audio3DParams;

	if (sourceVoiceHandle->params.is3D)
		Calculate3DEffect(sourceVoiceHandle, submitvoice);

	hr = voice->SubmitSourceBuffer(&sourceVoiceHandle->audioData.buffer);
	if (FAILED(hr)) {
		sourceVoiceHandle->source->DestroyVoice();
		sourceVoiceHandle->source = nullptr;
		SAFE_DELETE(sourceVoiceHandle->audioData.buffer.pContext);
		return AudioPlayHandle();
	}

	m_HandleToSourceVoiceHandle.EnsureInsert(handle, sourceVoiceHandle);

	hr = voice->Start(0);
	if (FAILED(hr)) {
		m_HandleToSourceVoiceHandle.Erase(handle);
		sourceVoiceHandle->source->DestroyVoice();
		sourceVoiceHandle->source = nullptr;
		SAFE_DELETE(sourceVoiceHandle->audioData.buffer.pContext);
		return AudioPlayHandle();
	}

	return handle;
}

bool AudioDevice::StopAudio(const AudioPlayHandle& playHandle)
{
	if (!m_HandleToSourceVoiceHandle.Exist(playHandle))
		return false;

	std::shared_ptr<SourceVoiceHandle> sourceVoiceHandle;
	{
		auto guard = m_HandleToSourceVoiceHandle.MakeLockGuard();
		if (!m_HandleToSourceVoiceHandle.Find(playHandle, sourceVoiceHandle))
			return false;
		m_HandleToSourceVoiceHandle.Erase(playHandle);
	}

	if (!sourceVoiceHandle || !sourceVoiceHandle->source)
		return false;

	sourceVoiceHandle->source->Stop();
	sourceVoiceHandle->source->DestroyVoice();
	sourceVoiceHandle->source = nullptr;
	SAFE_DELETE(sourceVoiceHandle->audioData.buffer.pContext);
	return true;
}

bool AudioDevice::IsPlaying(const AudioPlayHandle& handle) const
{
	std::shared_ptr<SourceVoiceHandle> sourceVoiceHandle;
	if (!m_HandleToSourceVoiceHandle.Find(handle, sourceVoiceHandle))
		return false;

	if (!sourceVoiceHandle || !sourceVoiceHandle->source)
		return false;

	XAUDIO2_VOICE_STATE state;
	sourceVoiceHandle->source->GetState(&state);

	return state.BuffersQueued > 0 || state.SamplesPlayed > 0;
}

bool AudioDevice::SetMasterVolumn(float volumn)
{
	if (!m_pXAudio2MasteringVoice)
		return false;

	volumn = std::clamp(volumn, 0.f, 1.f);
	m_pXAudio2MasteringVoice->SetVolume(volumn);
	return true;
}

void AudioDevice::OnBufferEnd(void* context)
{
	if (!context) return;

	AudioPlayHandle* playHandle = static_cast<AudioPlayHandle*>(context);

	bool needclose = false;
	std::shared_ptr<SourceVoiceHandle> sourceVoiceHandle;
	{
		auto guard = m_HandleToSourceVoiceHandle.MakeLockGuard();
		if (!m_HandleToSourceVoiceHandle.Find(*playHandle, sourceVoiceHandle))
			return;

		if (!sourceVoiceHandle || !sourceVoiceHandle->source)
		{
			m_HandleToSourceVoiceHandle.Erase(*playHandle);
			return;
		}

		XAUDIO2_VOICE_STATE state;
		sourceVoiceHandle->source->GetState(&state);
		needclose = state.BuffersQueued == 0;
		if (needclose)
			m_HandleToSourceVoiceHandle.Erase(*playHandle);
	}

	if (sourceVoiceHandle)
	{
		if (needclose) 	// 播放完成
		{
			sourceVoiceHandle->source->Stop();
			sourceVoiceHandle->source->DestroyVoice();
			sourceVoiceHandle->source = nullptr;
			SAFE_DELETE(sourceVoiceHandle->audioData.buffer.pContext);
			if (sourceVoiceHandle->onComplateFunc)
			{
				try
				{
					sourceVoiceHandle->onComplateFunc(*playHandle);
				}
				catch (const std::exception&)
				{
				}
			}
		}
	}
}

void AudioDevice::OnVoiceError(void* context, HRESULT Error)
{
	if (!context) return;

	bool needrestore = Error == XAUDIO2_E_DEVICE_INVALIDATED;
	bool needclose = !needrestore;

	AudioPlayHandle* playHandle = static_cast<AudioPlayHandle*>(context);

	std::shared_ptr<SourceVoiceHandle> sourceVoiceHandle;
	{
		auto guard = m_HandleToSourceVoiceHandle.MakeLockGuard();
		if (!m_HandleToSourceVoiceHandle.Find(*playHandle, sourceVoiceHandle))
			return;

		if (!sourceVoiceHandle || !sourceVoiceHandle->source)
		{
			m_HandleToSourceVoiceHandle.Erase(*playHandle);
			return;
		}
		if (needclose)
			m_HandleToSourceVoiceHandle.Erase(*playHandle);
	}

	if (sourceVoiceHandle)
	{
		if (needclose)
		{
			sourceVoiceHandle->source->Stop();
			sourceVoiceHandle->source->DestroyVoice();
			sourceVoiceHandle->source = nullptr;
			SAFE_DELETE(sourceVoiceHandle->audioData.buffer.pContext);
			if (sourceVoiceHandle->onComplateFunc)
			{
				try
				{
					sourceVoiceHandle->onComplateFunc(*playHandle);
				}
				catch (const std::exception&)
				{
				}
			}
		}

		if (needrestore)
			RestoreVoiceDevice();
	}
}

bool AudioDevice::AddChannel(AudioChannelID id)
{
	if (m_ChannelIDToSubmixVoice.Exist(id))
		return false;

	auto guard = m_ChannelIDToSubmixVoice.MakeLockGuard();
	if (m_ChannelIDToSubmixVoice.Exist(id))
		return false;

	if (!m_xAudio2 || !m_pXAudio2MasteringVoice)
		return false;

	// 获取主音频格式
	XAUDIO2_VOICE_DETAILS targetDetails;
	m_pXAudio2MasteringVoice->GetVoiceDetails((XAUDIO2_VOICE_DETAILS*)&targetDetails);

	IXAudio2SubmixVoice* submitVoice = nullptr;
	HRESULT hr = m_xAudio2->CreateSubmixVoice(&submitVoice, targetDetails.InputChannels, targetDetails.InputSampleRate);

	//IXAudio2SubmixVoice* submitVoice = nullptr;
	//HRESULT hr = m_xAudio2->CreateSubmixVoice(&submitVoice, 2, 44100);
	if (FAILED(hr))
		return false;

	m_ChannelIDToSubmixVoice.EnsureInsert(id, submitVoice);
	return true;
}

bool AudioDevice::SetChannelVolumn(AudioChannelID id, float volumn)
{
	if (!m_ChannelIDToSubmixVoice.Exist(id))
		return AddChannel(id);

	auto guard = m_ChannelIDToSubmixVoice.MakeLockGuard();
	IXAudio2SubmixVoice* submitvoice = nullptr;
	if (!m_ChannelIDToSubmixVoice.Find(id, submitvoice) || !submitvoice)
		return false;

	volumn = std::clamp(volumn, 0.f, 1.f);
	submitvoice->SetVolume(volumn);
	return true;
}

void AudioDevice::SetListener(const AudioListener& listener)
{
	_listener = listener;
	_listenerDirty = true;
}

void AudioDevice::SetSource3DParams(const AudioPlayHandle& handle, const Audio3DParams& params)
{
	std::shared_ptr<SourceVoiceHandle> sourceVoiceHandle;
	if (!m_HandleToSourceVoiceHandle.Find(handle, sourceVoiceHandle) || !sourceVoiceHandle)
		return;

	sourceVoiceHandle->params = params;
}

void AudioDevice::Update3DAudio()
{
	if (m_deviceState != DeviceState::Normal) return;

	auto guard = m_HandleToSourceVoiceHandle.MakeLockGuard();

	m_HandleToSourceVoiceHandle.EnsureCall(
		[&](std::unordered_map<AudioPlayHandle, std::shared_ptr<SourceVoiceHandle>>& map)->void
		{
			for (auto& [handle, sourceVoiceHandle] : map)
			{
				if (!sourceVoiceHandle || !sourceVoiceHandle->source) continue;
				if (!sourceVoiceHandle->params.is3D) continue;

				IXAudio2SubmixVoice* targetVoice = nullptr;
				if (!m_ChannelIDToSubmixVoice.Find(sourceVoiceHandle->channelId, targetVoice) || !targetVoice)
					continue;

				Calculate3DEffect(sourceVoiceHandle, targetVoice);
			}
		}
	);
}

bool AudioDevice::InitVoiceDevice()
{
	m_deviceState = DeviceState::Failed;

	if (m_xAudio2)
	{
		m_xAudio2->Release();
		m_xAudio2 = nullptr;
	}
	if (m_pXAudio2MasteringVoice)
	{
		m_pXAudio2MasteringVoice->DestroyVoice();
		m_pXAudio2MasteringVoice = nullptr;
	}

	HRESULT hr = S_OK;

	hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(hr))
		return false;

	hr = ::XAudio2Create(&m_xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
	if (FAILED(hr))
		return false;

	hr = m_xAudio2->CreateMasteringVoice(&m_pXAudio2MasteringVoice);
	if (FAILED(hr))
	{
		if (m_xAudio2)
		{
			m_xAudio2->Release();
			m_xAudio2 = nullptr;
		}
		return false;
	}

	if (!AddChannel(0))
	{
		if (m_xAudio2)
		{
			m_xAudio2->Release();
			m_xAudio2 = nullptr;
		}
		if (m_pXAudio2MasteringVoice)
		{
			m_pXAudio2MasteringVoice->DestroyVoice();
			m_pXAudio2MasteringVoice = nullptr;
		}
		return false;
	}


	hr = m_pXAudio2MasteringVoice->GetChannelMask(&_channelMask);
	if (FAILED(hr))
	{
		if (m_xAudio2)
		{
			m_xAudio2->Release();
			m_xAudio2 = nullptr;
		}
		if (m_pXAudio2MasteringVoice)
		{
			m_pXAudio2MasteringVoice->DestroyVoice();
			m_pXAudio2MasteringVoice = nullptr;
		}
		m_ChannelIDToSubmixVoice.Erase(0);
		return false;
	}

	hr = X3DAudioInitialize(_channelMask, X3DAUDIO_SPEED_OF_SOUND, _x3DInstance);
	if (FAILED(hr))
	{
		if (m_xAudio2)
		{
			m_xAudio2->Release();
			m_xAudio2 = nullptr;
		}
		if (m_pXAudio2MasteringVoice)
		{
			m_pXAudio2MasteringVoice->DestroyVoice();
			m_pXAudio2MasteringVoice = nullptr;
		}
		m_ChannelIDToSubmixVoice.Erase(0);
		_channelMask = 0;
		return false;
	}

	_listener.position = { 0, 0, 0 };
	_listener.forward = { 0, 0, 1 };
	_listener.up = { 0, 1, 0 };
	_listener.velocity = { 0, 0, 0 };

	m_deviceState = DeviceState::Normal;
	return true;
}

void AudioDevice::RestoreVoiceDevice()
{
	// 设备失效处理
	DeviceState expected = DeviceState::Normal;
	if (!m_deviceState.compare_exchange_strong(expected, DeviceState::Recovering))
	{
		return;
	}

	auto guard = m_HandleToSourceVoiceHandle.MakeLockGuard();
	m_HandleToSourceVoiceHandle.EnsureCall(
		[&](std::unordered_map<AudioPlayHandle, std::shared_ptr<SourceVoiceHandle>>& map)->void {
			for (auto& pair : map) {
				if (pair.second->source)
				{
					pair.second->source->Stop();
					pair.second->source->DestroyVoice();
					pair.second->source = nullptr;
				}
			}
		}
	);

	if (InitVoiceDevice())
	{
		int success = 0;
		int failed = 0;

		auto RestoreVoice = [&](std::shared_ptr<SourceVoiceHandle>& sourceVoiceHandle)->bool {
			if (!sourceVoiceHandle || m_deviceState != DeviceState::Normal || !m_xAudio2)
				return false;

			HRESULT hr = S_OK;

			if (!m_ChannelIDToSubmixVoice.Exist(sourceVoiceHandle->channelId))
				AddChannel(sourceVoiceHandle->channelId);

			IXAudio2SubmixVoice* submitvoice = nullptr;
			if (!m_ChannelIDToSubmixVoice.Find(sourceVoiceHandle->channelId, submitvoice) || !submitvoice)
				return false;

			XAUDIO2_SEND_DESCRIPTOR sendDesc = { 0, submitvoice };
			XAUDIO2_VOICE_SENDS sendList = { 1, &sendDesc };

			IXAudio2SourceVoice* newVoice;
			hr = m_xAudio2->CreateSourceVoice(
				&newVoice,
				(WAVEFORMATEX*)&sourceVoiceHandle->audioData.wfx,
				0,
				XAUDIO2_DEFAULT_FREQ_RATIO,
				this,
				&sendList,
				nullptr
			);
			if (FAILED(hr) || !newVoice)
				return false;

			sourceVoiceHandle->source = newVoice;

			hr = newVoice->SubmitSourceBuffer(&sourceVoiceHandle->audioData.buffer);
			if (FAILED(hr)) {
				sourceVoiceHandle->source->DestroyVoice();
				sourceVoiceHandle->source = nullptr;
				SAFE_DELETE(sourceVoiceHandle->audioData.buffer.pContext);
				return false;
			}

			hr = newVoice->Start(0);
			if (FAILED(hr)) {
				sourceVoiceHandle->source->DestroyVoice();
				sourceVoiceHandle->source = nullptr;
				SAFE_DELETE(sourceVoiceHandle->audioData.buffer.pContext);
				return false;
			}

			return true;
			};
		m_HandleToSourceVoiceHandle.EnsureCall(
			[&](std::unordered_map<AudioPlayHandle, std::shared_ptr<SourceVoiceHandle>>& map)->void {
				for (auto& pair : map) {
					if (RestoreVoice(pair.second)) {
						success++;
					}
					else {
						failed++;
					}
				}
			}
		);
	}
}

void AudioDevice::CleanUp()
{
	m_HandleToSourceVoiceHandle.EnsureCall(
		[&](std::unordered_map<AudioPlayHandle, std::shared_ptr<SourceVoiceHandle>>& map)->void {
			for (auto& pair : map) {
				if (pair.second->source)
				{
					pair.second->source->Stop();
					pair.second->source->DestroyVoice();
				}
			}
			map.clear();
		}
	);
	m_ChannelIDToSubmixVoice.EnsureCall(
		[&](std::unordered_map<AudioChannelID, IXAudio2SubmixVoice*>& map)->void {
			for (auto& pair : map) {
				if (pair.second)
				{
					pair.second->DestroyVoice();
					pair.second = nullptr;
				}
			}
			map.clear();
		}
	);

	if (m_pXAudio2MasteringVoice)
	{
		m_pXAudio2MasteringVoice->DestroyVoice();
		m_pXAudio2MasteringVoice = nullptr;
	}
	if (m_xAudio2)
	{
		m_xAudio2->Release();
		m_xAudio2 = nullptr;
	}
}

void AudioDevice::Calculate3DEffect(std::shared_ptr<SourceVoiceHandle>& sourceVoiceHandle, IXAudio2SubmixVoice* targetVoice)
{
	if (!sourceVoiceHandle || !targetVoice)
		return;

	// 监听器
	auto listener = _listener;
	X3DAUDIO_LISTENER x3d_listener = { };
	x3d_listener.Position = listener.position;
	x3d_listener.Velocity = listener.velocity;
	x3d_listener.OrientFront = listener.forward;
	x3d_listener.OrientTop = listener.up;
	x3d_listener.pCone = nullptr;

	UINT32 srcChannel = 0;
	std::vector<float> channelAzimuths;
	GetChannelAzimuthsFromWFX(sourceVoiceHandle->audioData.wfx, srcChannel, channelAzimuths);

	// 计算3D参数
	UINT32 destChannel = 0;
	GetChannelCount(_channelMask, destChannel);
	std::vector<float> matrix(srcChannel * destChannel);
	std::vector<float> delayTimes(destChannel, 0.f);

	X3DAUDIO_DSP_SETTINGS dspSettings = { 0 };
	dspSettings.SrcChannelCount = srcChannel;
	dspSettings.DstChannelCount = destChannel;
	dspSettings.pMatrixCoefficients = matrix.data();
	dspSettings.pDelayTimes = delayTimes.data();


	// 准备X3DAudio计算
	X3DAUDIO_EMITTER x3d_emitter = { 0 };
	x3d_emitter.ChannelCount = srcChannel;
	x3d_emitter.ChannelRadius = 1.0f;
	x3d_emitter.pChannelAzimuths = channelAzimuths.data();
	x3d_emitter.OrientFront = sourceVoiceHandle->params.forward;
	x3d_emitter.OrientTop = sourceVoiceHandle->params.up;
	x3d_emitter.Position = sourceVoiceHandle->params.position;
	x3d_emitter.Velocity = sourceVoiceHandle->params.velocity;

	// 距离衰减
	x3d_emitter.CurveDistanceScaler = sourceVoiceHandle->params.maxDistance;
	x3d_emitter.DopplerScaler = 1.0f;

	// 方向锥
	X3DAUDIO_CONE myCone = { 0 };
	myCone.InnerAngle = sourceVoiceHandle->params.innerAngle;
	myCone.InnerVolume = sourceVoiceHandle->params.innerVolume;
	myCone.InnerLPF = sourceVoiceHandle->params.innerLPF;
	myCone.InnerReverb = sourceVoiceHandle->params.innerReverb;
	myCone.OuterAngle = sourceVoiceHandle->params.outerAngle;
	myCone.OuterVolume = sourceVoiceHandle->params.outerVolume;
	myCone.OuterLPF = sourceVoiceHandle->params.outerLPF;
	myCone.OuterReverb = sourceVoiceHandle->params.outerReverb;
	x3d_emitter.pCone = &myCone;
	//x3d_emitter.pCone = nullptr;

	static X3DAUDIO_DISTANCE_CURVE_POINT volumePoints[] = {
		{0.00f, 1.00f},
		{0.10f, 0.95f},
		{0.30f, 0.70f},
		{0.55f, 0.40f},
		{0.80f, 0.15f},
		{1.00f, 0.02f}
	};

	X3DAUDIO_DISTANCE_CURVE volumeCurve = { volumePoints, 6 };
	x3d_emitter.pVolumeCurve = &volumeCurve;

	X3DAudioCalculate(_x3DInstance, &x3d_listener, &x3d_emitter,
		X3DAUDIO_CALCULATE_MATRIX |
		X3DAUDIO_CALCULATE_DOPPLER |
		X3DAUDIO_CALCULATE_LPF_DIRECT |
		X3DAUDIO_CALCULATE_REVERB,
		&dspSettings);

	// 打印关键信息
	//printf("DopplerFactor: %f\n", dspSettings.DopplerFactor);
	//printf("LPFDirectCoefficient: %f\n", dspSettings.LPFDirectCoefficient);
	//printf("LPFReverbCoefficient: %f\n", dspSettings.LPFReverbCoefficient);
	//std::vector<std::string> channelName;
	//GetChannelName(_channelMask, channelName);
	//std::cout << "MatrixStart ===================\n";
	//for (int i = 0; i < dspSettings.DstChannelCount; i++) {
	//	std::cout << channelName[i] << '\t';
	//	int start = i * dspSettings.SrcChannelCount;
	//	for (int j = 0; j < dspSettings.SrcChannelCount; j++) {
	//		std::cout << matrix[start + j] << '\t';
	//	}
	//	std::cout << "\n";
	//}
	//std::cout << "MatrixEnd ===================\n";

	// 应用结果到XAudio2
	if (targetVoice && dspSettings.DstChannelCount > 0) {
		// 应用矩阵系数
		HRESULT hr = sourceVoiceHandle->source->SetOutputMatrix(
			targetVoice,
			dspSettings.SrcChannelCount,
			dspSettings.DstChannelCount,
			dspSettings.pMatrixCoefficients
		);
		if (FAILED(hr)) {
			//std::cout << std::format("error \n");
		}
	}

	// 应用多普勒效应
	sourceVoiceHandle->source->SetFrequencyRatio(listener.dopplerFactor * dspSettings.DopplerFactor);

	// 应用低通滤波（远处声音变闷）
	float LPF = dspSettings.LPFDirectCoefficient * sourceVoiceHandle->params.occlusionLPF;
	if (LPF > 0)
	{
		float cutoffFrequency = 20.0f * powf(1000.0f, LPF);
		cutoffFrequency = std::clamp(cutoffFrequency, 20.0f, 20000.0f);

		// 将 LPFDirectCoefficient 映射为绝对截止频率 (Hz)
		// 用系数 0.0~1.0 映射到 20Hz ~ 奈奎斯特频率
		float sampleRate = static_cast<float>(sourceVoiceHandle->audioData.wfx.Format.nSamplesPerSec);
		float nyquist = sampleRate * 0.5f;
		float cutoffHz = 20.0f + (nyquist - 20.0f) * LPF;
		cutoffHz = std::clamp(cutoffHz, 20.0f, nyquist);

		// 使用 XAudio2 提供的转换函数获取归一化频率
		float normalizedFrequency = XAudio2CutoffFrequencyToOnePoleCoefficient(cutoffHz, sampleRate);

		// 构造滤波器参数
		XAUDIO2_FILTER_PARAMETERS filterParams = { };
		filterParams.Type = LowPassFilter;  // 低通滤波器
		filterParams.Frequency = normalizedFrequency;
		filterParams.OneOverQ = 1.0f;

		HRESULT hr = sourceVoiceHandle->source->SetFilterParameters(&filterParams);
		if (FAILED(hr)) {}
	}
}

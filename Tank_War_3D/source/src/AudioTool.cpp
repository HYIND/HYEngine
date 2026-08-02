#include "AudioProc/AudioTool.h"
#include "Net/Helper/Buffer.h"
#include "Helper/Tools.h"

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

AudioPlayHandle AudioDevice::PlayAudio(const AudioInfo& audio, std::function<void(AudioPlayHandle)> complateCallback, AudioChannelID channelId)
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

	if (!m_xAudio2)
		return false;

	IXAudio2SubmixVoice* submitVoice = nullptr;
	HRESULT hr = m_xAudio2->CreateSubmixVoice(&submitVoice, 2, 44100);
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
	}

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

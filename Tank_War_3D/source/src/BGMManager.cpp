#include "Manager/BGMManager.h"
#include "Manager/AudioDeviceManager.h"
#include "Manager/ResourceManager.h"

BGMManager* BGMManager::Instance()
{
	static BGMManager* m_instance = new BGMManager();
	return m_instance;
}

BGMManager::BGMManager()
	:_enabled(true)
{

}

void BGMManager::SetEnableCurBGM()
{
	LockGuard guard(_playing_data_lock);

	bool needstop = _playing_bgm_res != _cur_bgm_res || !_enabled;
	if (needstop)
	{
		if (_playing_audioPlayhandle)
		{
			if (auto device = AudioDeviceManager::Instance()->GetDevice())
			{
				AudioDeviceManager::Instance()->GetDevice()->StopAudio(_playing_audioPlayhandle);
				_playing_audioPlayhandle = AudioPlayHandle();
				_playing_bgm_res = "";
			}
		}
		if (_enabled)
		{
			_playing_bgm_res = _cur_bgm_res;
			if (auto audio = ResFactory->GetAudioRes(_playing_bgm_res))
			{
				if (auto device = AudioDeviceManager::Instance()->GetDevice())
				{
					_playing_audioPlayhandle = device->PlayAudio(*audio,
						std::bind(&BGMManager::OnOncePlayEnd, this, std::placeholders::_1),
						(AudioChannelID)AudioChannelDef::BGM_Channel);

					if (!_playing_audioPlayhandle)
						_playing_bgm_res = "";
				}
			}
		}
	}
}



void BGMManager::OnOncePlayEnd(AudioPlayHandle playHandle)
{
	{
		LockGuard guard(_playing_data_lock);
		if (playHandle == _playing_audioPlayhandle)
		{
			_playing_audioPlayhandle = AudioPlayHandle();
			_playing_bgm_res = "";
		}
	}
	RePlayWithDelay();
}

Task<void> BGMManager::RePlayWithDelay()
{
	co_await CoSleep(std::chrono::seconds(1));
	SetEnableCurBGM();
	co_return;
}

void BGMManager::SetBGMEnable(bool enabled)
{
	if (_enabled != enabled)
	{
		_enabled = enabled;
		SetEnableCurBGM();
	}
}

void BGMManager::SetScene(STATUS status)
{
	switch (status)
	{
	case STATUS::Room_Status:
	{
		PlayMenuBGM();
		break;
	}
	case STATUS::Main:
	{
		PlayMenuBGM();
		break;
	}
	case STATUS::Option:
	{
		PlayMenuBGM();
		break;
	}
	case STATUS::Hall_Status:
	{
		PlayMenuBGM();
		break;
	}
	case STATUS::LocalGame_Status:
	{
		PlayGameBGM();
		break;
	}
	case STATUS::OnlineGame_Status:
	{
		PlayGameBGM();
		break;
	}
	default:
		break;
	}
}

void BGMManager::PlayMenuBGM()
{
	_cur_bgm_res = ResName::MenuBGM;
	SetEnableCurBGM();
}

void BGMManager::PlayGameBGM()
{
	_cur_bgm_res = ResName::GameBGM;
	SetEnableCurBGM();
}

bool BGMManager::BGMEnable() const
{
	return _enabled;
}

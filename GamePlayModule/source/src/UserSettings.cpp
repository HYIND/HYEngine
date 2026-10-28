#include "UserSettings.h"

#include <algorithm>

#include <windows.h>
#include <tchar.h>

#include "SceneCodec/ModuleHostServices.h"
#include "UI/EngineCommands.h"
#include "UI/MessageBus.h"
#include "Helper/AudioTool.h"

namespace
{
	int ReadInt(const TCHAR* path, const TCHAR* section, const TCHAR* key, int fallback)
	{
		return static_cast<int>(::GetPrivateProfileInt(section, key, fallback, path));
	}

	bool WriteInt(const TCHAR* path, const TCHAR* section, const TCHAR* key, int value)
	{
		// 用 TCHAR 版本的写入口，和读入口对称；数字先格式化成 TCHAR 缓冲。
		TCHAR text[16] = {};
		_stprintf_s(text, _T("%d"), value);
		return ::WritePrivateProfileString(section, key, text, path) != 0;
	}
}

void UserSettings::Load(const TCHAR* path)
{
	// 键名和 Game 原来的 option.ini 保持一致，老文件不用改。
	m_targetFps = std::clamp(ReadInt(path, _T("Video"), _T("FPS"), 144), 0, 500);
	m_masterVolume = std::clamp(ReadInt(path, _T("Audio"), _T("MasterVolume"), 80), 0, 100);
	m_musicVolume = std::clamp(ReadInt(path, _T("Audio"), _T("MusicVolume"), 80), 0, 100);
	m_sfxVolume = std::clamp(ReadInt(path, _T("Audio"), _T("SFXVolume"), 80), 0, 100);
}

bool UserSettings::Save(const TCHAR* path) const
{
	// 四条都写：漏一条就会留下"界面上是 30、文件里还是 80"的状态。
	bool ok = WriteInt(path, _T("Video"), _T("FPS"), m_targetFps);
	ok = WriteInt(path, _T("Audio"), _T("MasterVolume"), m_masterVolume) && ok;
	ok = WriteInt(path, _T("Audio"), _T("MusicVolume"), m_musicVolume) && ok;
	ok = WriteInt(path, _T("Audio"), _T("SFXVolume"), m_sfxVolume) && ok;
	return ok;
}

void UserSettings::Apply(const NComponentCodec::ModuleHostServices& services) const
{
	if (!services.messageBus)
		return;

	services.messageBus->Post(UI::EngineCommands::MakeTargetFps(m_targetFps));
	services.messageBus->Post(UI::EngineCommands::MakeMasterVolume(m_masterVolume));
	services.messageBus->Post(UI::EngineCommands::MakeChannelVolume(uint32_t(AudioChannelDef::BGM_Channel), m_musicVolume));
	services.messageBus->Post(UI::EngineCommands::MakeChannelVolume(uint32_t(AudioChannelDef::SoundEffects_Channel), m_sfxVolume));
}

void UserSettings::SetTargetFps(int fps)
{
	m_targetFps = std::clamp(fps, 0, 500);
}

void UserSettings::SetMasterVolume(int percent)
{
	m_masterVolume = std::clamp(percent, 0, 100);
}

void UserSettings::SetMusicVolume(int percent)
{
	m_musicVolume = std::clamp(percent, 0, 100);
}

void UserSettings::SetSfxVolume(int percent)
{
	m_sfxVolume = std::clamp(percent, 0, 100);
}

UserSettings& GetUserSettings()
{
	static UserSettings settings;
	return settings;
}

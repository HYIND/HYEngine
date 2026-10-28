#pragma once

#include <tchar.h>

namespace NComponentCodec { struct ModuleHostServices; }

class UserSettings
{
public:
	static constexpr const TCHAR* DefaultPath = _T("./option.ini");

	void Load(const TCHAR* path = DefaultPath);
	bool Save(const TCHAR* path = DefaultPath) const;

	// 把当前四项 Post 成 engine.settings.*，引擎下一次派发时生效。
	void Apply(const NComponentCodec::ModuleHostServices& services) const;

	int TargetFps() const { return m_targetFps; }
	int MasterVolume() const { return m_masterVolume; }
	int MusicVolume() const { return m_musicVolume; }
	int SfxVolume() const { return m_sfxVolume; }

	void SetTargetFps(int fps);
	void SetMasterVolume(int percent);
	void SetMusicVolume(int percent);
	void SetSfxVolume(int percent);

private:
	int m_targetFps = 144;
	int m_masterVolume = 80;
	int m_musicVolume = 80;
	int m_sfxVolume = 80;
};

UserSettings& GetUserSettings();

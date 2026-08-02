#pragma once

#include "Coroutine.h"
#include "AudioProc/AudioTool.h"
#include "Scene.h"

class BGMManager {
public:
	static BGMManager* Instance();

	void SetBGMEnable(bool enabled);

	void SetScene(STATUS status);

	void PlayMenuBGM();
	void PlayGameBGM();

	bool BGMEnable() const;
private:
	BGMManager();
	void SetEnableCurBGM();
	void OnOncePlayEnd(AudioPlayHandle playHandle);
	Task<void> RePlayWithDelay();

private:
	bool _enabled;
	std::string _cur_bgm_res;

	std::string _playing_bgm_res;
	AudioPlayHandle _playing_audioPlayhandle;
	CriticalSectionLock _playing_data_lock;
};

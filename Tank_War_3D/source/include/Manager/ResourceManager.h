#pragma once

#include "RenderEngine/D2DTools.h"
#include "AudioProc/AudioTool.h"
#include "OpenGLRenderEngine/Base/Model.h"
#include "OpenGLRenderEngine/Base/Texture2D.h"
#include "OpenGLRenderEngine/Base/Animation.h"
#include <map>
#include <iostream>

namespace ResName
{
	const std::string Test = "Test";

	// UI用
	const std::string textBK = "textBK";
	const std::string returnBP = "returnBP";
	const std::string pauseBP = "pauseBP";
	const std::string winBP = "winBP";
	const std::string failBP = "failBP";
	const std::string opBK = "opBK";

	//Audio
	const std::string healAudio = "healAudio";
	const std::string defaultShootAudio = "defaultShootAudio";
	const std::string defaultAttackedAudio = "defaultAttackedAudio";

	const std::string MenuBGM = "MenuBGM";
	const std::string GameBGM = "GameBGM";

	//Tex2D
	const std::string EmptyTexture = "EmptyTexture";

	//TexCube
	const std::string EmptyCubeMap = "EmptyCubeMap";	//空纹理，着色器占位用
	const std::string skybox1 = "skybox1";
	const std::string skybox2 = "skybox2";

	//Model
	const std::string Quad = "Quad";
	const std::string Cube = "Cube";
	const std::string Sphere = "Sphere";
	const std::string Cylinder = "Cylinder";

	const std::string Keqing1 = "Keqing1";
	const std::string Keqing2 = "Keqing2";
	const std::string Dust2 = "Dust2";
	const std::string NewDust2 = "NewDust2";
	const std::string AK47 = "AK47";
	const std::string Pistol = "Pistol";
	const std::string Sniper = "Sniper";
	const std::string SMG = "SMG";

	const std::string lowPolyForest = "lowPolyForest";

	const std::string Sponza = "Sponza";

	//Animation
	const std::string AK47_Anim_Static =			"AK47_Anim_Static";
	const std::string AK47_Anim_Draw =				"AK47_Anim_Draw";
	const std::string AK47_Anim_Idle =				"AK47_Anim_Idle";
	const std::string AK47_Anim_Reload =			"AK47_Anim_Reload";
	const std::string AK47_Anim_Reload_Full =		"AK47_Anim_Reload_Full";
	const std::string AK47_Anim_Run =				"AK47_Anim_Run";
	const std::string AK47_Anim_Shot =				"AK47_Anim_Shot";
	const std::string AK47_Anim_Walk =				"AK47_Anim_Walk";

	const std::string Pistol_Anim_Static =			"Pistol_Anim_Static";
	const std::string Pistol_Anim_Draw =			"Pistol_Anim_Draw";
	const std::string Pistol_Anim_Idle =			"Pistol_Anim_Idle";
	const std::string Pistol_Anim_Reload =			"Pistol_Anim_Reload";
	const std::string Pistol_Anim_Reload_Full =		"Pistol_Anim_Reload_Full";
	const std::string Pistol_Anim_Run =				"Pistol_Anim_Run";
	const std::string Pistol_Anim_Shot =			"Pistol_Anim_Shot";
	const std::string Pistol_Anim_Walk =			"Pistol_Anim_Walk";

	const std::string Sniper_Anim_Static =			"Sniper_Anim_Static";
	const std::string Sniper_Anim_Draw =			"Sniper_Anim_Draw";
	const std::string Sniper_Anim_Idle =			"Sniper_Anim_Idle";
	const std::string Sniper_Anim_Reload =			"Sniper_Anim_Reload";
	const std::string Sniper_Anim_Reload_Full =		"Sniper_Anim_Reload_Full";
	const std::string Sniper_Anim_Run =				"Sniper_Anim_Run";
	const std::string Sniper_Anim_Shot =			"Sniper_Anim_Shot";
	const std::string Sniper_Anim_Walk =			"Sniper_Anim_Walk";

	const std::string SMG_Anim_Static =				"SMG_Anim_Static";
	const std::string SMG_Anim_Draw =				"SMG_Anim_Draw";
	const std::string SMG_Anim_Idle =				"SMG_Anim_Idle";
	const std::string SMG_Anim_Reload =				"SMG_Anim_Reload";
	const std::string SMG_Anim_Reload_Full =		"SMG_Anim_Reload_Full";
	const std::string SMG_Anim_Run =				"SMG_Anim_Run";
	const std::string SMG_Anim_Shot =				"SMG_Anim_Shot";
	const std::string SMG_Anim_Walk =				"SMG_Anim_Walk";

}

class GIFINFO {
public:
	GIFINFO(float mstime, UINT frameCount, IWICBitmapDecoder* pDecoder, IWICStream* pStream);
	~GIFINFO();
	float getDefaultMsTime();
	ID2D1Bitmap* getFrame(UINT frameNum);
	UINT getFrameCount();
private:
	float defaultTime = 0;
	UINT totalFrameCount = 0;
	IWICBitmapDecoder* pDecoder = NULL;
	IWICStream* pStream = NULL;
	std::map<UINT, ID2D1Bitmap*> _BitMaps;
};

class ResourceManager
{
public:
	static ResourceManager* Instance();

	bool InitD2DResource();
	bool InitAudioResource();
	bool InitOpenGLResource();

	ID2D1Bitmap* GetBitMapRes(const std::string& name);
	GIFINFO* GetGIFRes(const std::string& name);
	std::shared_ptr<AudioInfo> GetAudioRes(const std::string& name);
	std::shared_ptr<Model> GetModelRes(const std::string& name);
	std::shared_ptr<Texture2D> GetTexture2DRes(const std::string& name);
	GLuint GetTextureCubeRes(const std::string& name);
	std::shared_ptr<Animation> GetAnimationRes(const std::string& name);
private:
	ResourceManager();
	bool InitOpenGLResourceInternal();

private:
	std::map <std::string, ID2D1Bitmap* > BitMapRes;
	std::map <std::string, GIFINFO* > GIFRes;
	std::map <std::string, std::shared_ptr<AudioInfo>> AUDIORes;
	std::map <std::string, std::shared_ptr<Model>> ModelRes;
	std::map <std::string, std::shared_ptr<Texture2D>> Texture2DRes;
	std::map <std::string, GLuint > TextureCubeRes;
	std::map <std::string, std::shared_ptr<Animation>> AnimationRes;

	std::once_flag initOpenGLFlag;
};

#define ResFactory ResourceManager::Instance()
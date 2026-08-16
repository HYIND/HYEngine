#pragma once

#include "stdafx.h"
#include "RenderEngine/D2DWrapper.h"
#include "GeneralManager/KeyMapManaer.h"

/*
 各场景按钮和控件的编号
 首位：3-Main,4-Hall,5-Room,6-Game,7-Option
 次位：3-按钮，4-控件
*/

#define IDB_LOCALGAME  3301
#define IDB_ENTERHALL  3302
#define IDB_OPTION	3303
#define IDB_MAPEDIT	3304
#define IDB_QUITGAME	3305

#define IDB_REFRESH 4301
#define IDB_HALL_SEND 4302
#define IDB_ENTERROOM 4303
#define IDB_CREATEROOM 4304
#define IDB_EXITHALL 4305
#define HALL_EDIT_IN 4401
#define EDIT_HALL 4402
#define HALL_ROOM_LIST 4403
#define HALL_USER_LIST 4404

#define IDB_READY 5301
#define IDB_CANCELREADY 5302
#define IDB_STARTGAME	5303
#define IDB_EXITROOM	5304
#define IDB_ROOM_SEND 5305
#define ROOM_EDIT_IN 5401
#define EDIT_ROOM 5402
#define ROOM_USER_LIST 5403

#define IDB_PAUSE 6301
#define IDB_RETURN 6302
#define ReturnInEndGame 6303
#define ReturnToHall 6304
#define ReLoadMap 6305

#define IDB_SETFPS_30 7301
#define IDB_SETFPS_60 7302
#define IDB_SETFPS_144 7303
#define IDB_EXITOPTION 7304
#define EDIT_EKY1_UP 7401
#define EDIT_EKY1_DOWN 7402
#define EDIT_EKY1_LEFT 7403
#define EDIT_EKY1_RIGHT 7404
#define EDIT_EKY1_FIRE 7405
#define EDIT_EKY2_UP 7406
#define EDIT_EKY2_DOWN 7407
#define EDIT_EKY2_LEFT 7408
#define EDIT_EKY2_RIGHT 7409
#define EDIT_EKY2_FIRE 7410

#define MAPEDIT_EXIT 8301
#define MAPEDIT_LOAD 8302
#define MAPEDIT_SAVE 8303

#define Enterroom 6666
#define START 7777
#define DISBANDINROOM 8888
#define DISBANDINEND 9999
#define WIN 666
#define FAIL 777

using namespace std;
using namespace D2D1;

//场景基类
class Scene;

//场景派生类
class Scene_Main;
class Scene_Hall;
class Scene_Option;
class Scene_Room;
class Scene_Room_Host;
class Scene_Room_NoHost;
class Scene_Gaming_local;
class Scene_Gaming_online;
class Scene_WinGame;
class Scene_FailGame;
class Scene_Pause;
class Scene_MapEdit;

/* 以下为内部变量的声明 */
namespace Brush
{
	extern ID2D1SolidColorBrush* pRed_Brush;
	extern ID2D1SolidColorBrush* pGreen_Brush;

	extern ID2D1SolidColorBrush* pHall_Brush;
	extern ID2D1SolidColorBrush* pHall_ClickBrush;

	extern ID2D1SolidColorBrush* pMain_Brush;
	extern ID2D1SolidColorBrush* pMain_ClickBrush;
}
namespace TextFormat
{
	extern IDWriteTextFormat* pMain_Format;
	extern IDWriteTextFormat* pHall_Format;
	extern IDWriteTextFormat* pPing_Format;
}

extern HFONT edit_listbox_front;

namespace _Scene
{
	extern Scene* CurScene;

	extern Scene_Main* SMain;
	extern Scene_Hall* SHall;
	extern Scene_Option* SOption;
	extern Scene_Room* SRoom;
	extern Scene_Gaming_local* SGaming_local;
	extern Scene_Gaming_online* SGaming_online;
	extern Scene_WinGame* SWinGame;
	extern Scene_FailGame* SFailGame;
	extern Scene_Pause* SPause;

}

extern HWND userid_in;

//status 枚举
enum class STATUS { Main, Option, Hall_Status, Room_Status, LocalGame_Status, OnlineGame_Status };

namespace MousePos
{
	extern int MoveX, MoveY, ClickX, ClickY;
}

//场景类
class Scene
{
protected:
	vector<D2D_Button*> Button_list;
	vector<D2D_Bitmap*> Bitmap_list;
	vector<D2D_Text*> Text_list;
	vector<D2D_GIF*> Gif_list;

	//两个用来监测选中变化的指针
	D2D_Text* Text_changed = NULL;
	D2D_Bitmap* Bitmap_changed = NULL;

public:
	Scene() {};
	//初始化
	Scene(ID2D1DeviceContext* pRenderTarget, IWICImagingFactory* pIWICFactory, IDWriteFactory* pIDWriteFactory) :
		pRenderTarget(pRenderTarget), pIWICFactory(pIWICFactory), pIDWriteFactory(pIDWriteFactory) {
	};

	virtual void Load(RECT& rect) = 0;

	//添加位图
	D2D_Bitmap* AddResourceBitmap(int loc1, int loc2, int loc3, int loc4, ID2D1Bitmap* pBitmap, float opacity = 1.0f);
	// 添加文本
	D2D_Text* AddText(int loc1, int loc2, int loc3, int loc4, const wchar_t* pwch,
		ID2D1SolidColorBrush* pDefaultBrush = Brush::pMain_Brush,
		ID2D1SolidColorBrush* pClickBrush = Brush::pMain_ClickBrush,
		IDWriteTextFormat* pTextFormat = TextFormat::pMain_Format,
		int id = 0
	);
	// 添加按钮
	D2D_Button* AddButton(int loc1, int loc2, int loc3, int loc4, int id);
	D2D_Button* AddButton(int loc1, int loc2, int loc3, int loc4, int id, D2D_Bitmap* Bitmap);
	D2D_Button* AddButton(int loc1, int loc2, int loc3, int loc4, int id, D2D_Text* Text);
	// 添加GIF
	D2D_GIF* AddGIF(int loc1, int loc2, int loc3, int loc4, GIFINFO* gifInfo, int loopCount = -1);

	// 修改按钮信息
	bool ModifyButton_Location(int id, int loc1, int loc2, int loc3, int loc4, bool offset);
	bool ModifyButton_ID(int oldid, int newid);
	bool ModifyButton_Text(int id, const wstring& newstr);
	bool ModifyButton_Bitmap(int id, int loc1, int loc2, int loc3, int loc4, bool offset, float opcaity);

	// 修改文字信息
	bool ModifyText_Context(int id, const wstring& newstr);

	//删除按钮 
	bool DeleteButton(int id);

	// 绘制函数
	void DrawScene(double time_diff);
	virtual void OnDrawScene(double time_diff);
	// 鼠标移动消息反馈函数
	void Move();
	virtual void OnMove();
	// 点击消息反馈函数
	void Click(bool isLButtonPress, bool isShiftPress);
	virtual void OnClick(bool isLButtonPress, bool isShiftPress);
	// 周期函数
	void Tick();
	virtual void OnTick() {};

	virtual void Show(bool enabled) {};

protected:
	ID2D1DeviceContext* pRenderTarget = NULL;
	IWICImagingFactory* pIWICFactory;
	IDWriteFactory* pIDWriteFactory = NULL;
};

class Scene_Option :public Scene
{
public:
	HWND key_FORWARD;
	HWND key_BACKWORD;
	HWND key_LEFT;
	HWND key_RIGHT;
	HWND key_JUMP;
	HWND key_RELOAD;

	HWND key_unuse_1;
	HWND key_unuse_2;
	HWND key_unuse_3;
	HWND key_unuse_4;

	vector<HWND*> HWND_Messager;
	map<HWND, Keyboard> key_map_set;

	HWND m_hMasterSlider;  // 音乐音量滑块
	HWND m_hMusicSlider;  // 音乐音量滑块
	HWND m_hSFXSlider;    // 音效音量滑块

	static constexpr UINT ID_TEXT_MASTERVOL = 2001;
	static constexpr UINT ID_TEXT_MUSIC = 2002;
	static constexpr UINT ID_TEXT_SFX = 2003;

	static constexpr UINT ID_SLIDER_MASTERVOL = 3001;
	static constexpr UINT ID_SLIDER_MUSIC = 3002;
	static constexpr UINT ID_SLIDER_SFX = 3003;

	using Scene::Scene;
	virtual void OnDrawScene(double time_diff);
	void DrawOption();
	void Get_Key();
	virtual void Load(RECT& rect);
	virtual void Show(bool enabled) override;

	void UpdateVolumeText(int id, UINT pos);
};

class Scene_Hall :public Scene
{
public:
	using Scene::Scene;
	HWND Hall;
	HWND Hall_room_list;
	HWND Hall_user_list;
	HWND Hall_edit_in;
	HWND edit_hall;

	virtual void Load(RECT& rect);
	void DrawHall();
	virtual void OnDrawScene(double time_diff);
	virtual void Show(bool enabled) override;
};

class Scene_Room :public Scene
{
public:
	bool isLoad;
	HWND Room_user_list;
	HWND Room_edit_in;
	HWND edit_room;

public:
	using Scene::Scene;
	void DrawRoom();
	virtual void OnDrawScene(double time_diff);
	virtual void Show(bool enabled) override;
	virtual void Load(RECT& rect);
	void SetScene(bool isHost, bool isready);
};

class Scene_Gaming_local :public Scene
{
	using Scene::Scene;
public:
	void DrawGame();
	virtual void OnDrawScene(double time_diff);
	virtual void Load(RECT& rect);
};

class Scene_Gaming_online :public Scene
{
	using Scene::Scene;
public:
	void DrawGame();
	virtual void OnDrawScene(double time_diff);
	virtual void Load(RECT& rect);
};

class Scene_WinGame :public Scene
{
	using Scene::Scene;
public:
	virtual void Load(RECT& rect);
};

class Scene_FailGame :public Scene
{
	using Scene::Scene;
public:
	virtual void Load(RECT& rect);
};

class Scene_Pause :public Scene
{
	using Scene::Scene;
public:
	virtual void Load(RECT& rect) {};
};

class Scene_Main :public Scene
{
	using Scene::Scene;
public:
	void DrawMain();
	virtual void OnDrawScene(double time_diff);
	virtual void Load(RECT& rect);
};

void Init_Scene();
void Set_CurScene(STATUS status);
STATUS Get_CurScene();

HWND CreateTooltip(HWND hwndParent, HWND hwndControl, LPCWSTR lpszText);

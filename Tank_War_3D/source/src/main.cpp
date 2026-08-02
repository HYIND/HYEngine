// WindowsProject1.cpp : 定义应用程序的入口点。
//
#include "header.h"
#include "RenderProcess.h"
#include "Scene.h"
#include "Manager/GameWorldManager.h"
#include "Manager/RenderManager.h"
#include "Manager/ConnectManager.h"
#include "Manager/RequestManager.h"
#include "Manager/LobbyManager.h"
#include "BusyLoaderDialog.h"
#include "Manager/AudioDeviceManager.h"
#include <commctrl.h>
#include "Manager/FocusManager.h"
#include "Manager/MouseManager.h"
#include "Manager/RenderContextManager.h"
#include "ECS/Systems/LocalInputSystem.h"

#define MAX_LOADSTRING 100

WCHAR szTitle[MAX_LOADSTRING] = L"Tank War";                  // 标题栏文本
WCHAR szWindowClass[MAX_LOADSTRING];            // 主窗口类名

void LoadOption()
{
	//FPS
	{
		UINT FPS = std::clamp(GetPrivateProfileInt(L"Video", L"FPS", 144, L"./option.ini"), UINT(1), UINT(500));
		RenderManager::Instance()->getfpsController()->setGameTargetFps(FPS);
	}

	//音频
	if (auto& device = AudioDeviceManager::Instance()->GetDevice())
	{
		{
			UINT masterVol = std::clamp(GetPrivateProfileInt(L"Audio", L"MasterVolume", 80, L"./option.ini"), UINT(0), UINT(100));
			float volume = float(masterVol) / 100.0f;
			if (auto device = AudioDeviceManager::Instance()->GetDevice())
				device->SetMasterVolumn(volume);
		}

		{
			UINT musicVol = std::clamp(GetPrivateProfileInt(L"Audio", L"MusicVolume", 80, L"./option.ini"), UINT(0), UINT(100));
			float volume = float(musicVol) / 100.0f;
			if (auto device = AudioDeviceManager::Instance()->GetDevice())
				device->SetChannelVolumn((AudioChannelID)AudioChannelDef::BGM_Channel, volume);
		}

		{
			UINT SFXVol = std::clamp(GetPrivateProfileInt(L"Audio", L"SFXVolume", 80, L"./option.ini"), UINT(0), UINT(100));
			float volume = float(SFXVol) / 100.0f;
			if (auto device = AudioDeviceManager::Instance()->GetDevice())
				device->SetChannelVolumn((AudioChannelID)AudioChannelDef::SoundEffects_Channel, volume);
		}
	}
}

bool Init_all_Resource()
{
	LOGINFO("Init", "Init_All");
	ResFactory->InitD2DResource();
	ResFactory->InitAudioResource();
	Init_Scene();
	return true;
}


bool ShowConsole = false;

// 创建并重定向控制台
bool CreateDebugConsole(const wchar_t* title = L"TankWar Debug Console") {
	// 1. 检查是否已有关联的控制台
	if (AttachConsole(ATTACH_PARENT_PROCESS)) {
		// 已经附加到父进程的控制台（比如从命令行启动）
		printf("Attached to parent console\n");
	}
	else {
		// 2. 创建新的控制台
		if (!AllocConsole()) {
			DWORD err = GetLastError();
			if (err == ERROR_ACCESS_DENIED) {
				// 已经有关联的控制台
				printf("Console already allocated\n");
			}
			else {
				printf("Failed to allocate console: %lu\n", err);
				return false;
			}
		}
	}

	// 3. 设置控制台标题
	SetConsoleTitleW(title);

	// 4. 获取标准输出句柄
	HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hOutput == INVALID_HANDLE_VALUE) {
		printf("Failed to get stdout handle\n");
		return false;
	}

	// 5. 获取控制台屏幕缓冲区信息
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(hOutput, &csbi);

	// 6. 设置控制台窗口大小和缓冲区大小
	SMALL_RECT rect = { 0, 0, 120, 900 };  // 宽度120字符，高度900行
	SetConsoleWindowInfo(hOutput, TRUE, &rect);

	COORD size = { 120, 1000 };  // 缓冲区比窗口稍大，支持滚动
	SetConsoleScreenBufferSize(hOutput, size);

	// 7. 关键步骤：重定向 C 运行时标准输入输出
	// 保存原来的标准输出
	int originalStdout = _dup(_fileno(stdout));
	int originalStderr = _dup(_fileno(stderr));
	int originalStdin = _dup(_fileno(stdin));

	// 重定向到控制台
	FILE* fp;
	freopen_s(&fp, "CONOUT$", "w", stdout);
	freopen_s(&fp, "CONOUT$", "w", stderr);
	freopen_s(&fp, "CONIN$", "r", stdin);

	// 8. 同步 C++ 标准流
	std::ios::sync_with_stdio();

	// 9. 清除缓冲区并测试输出
	std::cout.clear();
	std::cin.clear();
	std::cerr.clear();
	std::clog.clear();

	// 10. 设置控制台编码（支持中文）
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);

	// 11. 测试输出
	printf("========================================\n");
	printf("        Debug Console Started\n");
	printf("========================================\n");
	printf("This is a test message\n");
	printf("中文测试 Chinese Test\n");
	printf("========================================\n");
	fflush(stdout);

	return true;
}

// 此代码模块中包含的函数的前向声明:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    Pause(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    Return(HWND, UINT, WPARAM, LPARAM);
BOOL CALLBACK GetID_Proc(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);


#ifdef _UNICODE
	for (int i = 0; i < __argc; i++) {
		if (__wargv[i] && wcsstr(__wargv[i], L"-console") != nullptr) {
			ShowConsole = true;
			break;
		}
	}
#else
	for (int i = 0; i < __argc; i++) {
		if (__argv[i] && strstr(__argv[i], "-console") != nullptr) {
			ShowConsole = true;
			break;
		}
	}
#endif

	// 初始化全局字符串
	LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadStringW(hInstance, IDC_WINDOWSPROJECT1, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance);

	// 执行应用程序初始化:
	if (!InitInstance(hInstance, nCmdShow))
	{
		return FALSE;
	}

	HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_WINDOWSPROJECT1));

	MSG msg;
	// 主消息循环:
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	return (int)msg.wParam;
}



//
//  函数: MyRegisterClass()
//
//  目标: 注册窗口类。
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_Tank_War));
	wcex.hCursor = LoadCursor(hInstance, MAKEINTRESOURCE(MYCURSOR));
	wcex.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_Tank_War));

	return RegisterClassExW(&wcex);
}

//
//   函数: InitInstance(HINSTANCE, int)
//
//   目标: 保存实例句柄并创建主窗口
//
//   注释:
//
//        在此函数中，我们在全局变量中保存实例句柄并
//        创建和显示主程序窗口。
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	RENDERCONTEXMANAGER->SetHinstance(hInstance);
	HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_VISIBLE | WS_OVERLAPPEDWINDOW ^ WS_THICKFRAME ^ WS_MAXIMIZEBOX | WS_CLIPCHILDREN | WS_EX_COMPOSITED | WS_EX_LAYERED,
		CW_USEDEFAULT, 0, 1600, 900, nullptr, nullptr, hInstance, nullptr);
	if (!hWnd)

	{
		return FALSE;
	}

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	return TRUE;
}

//
//  函数: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  目标: 处理主窗口的消息。
//
//  WM_COMMAND  - 处理应用程序菜单
//  WM_PAINT    - 绘制主窗口
//  WM_DESTROY  - 发送退出消息并返回
//
//

enum { hall_refreash, room_refreash, sceneTick };	//定时器ID
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_CREATE:
	{
		RECT rect;
		GetClientRect(hWnd, &rect);
		RENDERCONTEXMANAGER->SetHwnd(hWnd);
		RENDERCONTEXMANAGER->SetRECT(rect);

		if (ShowConsole)
			CreateDebugConsole();

		bool result = Init_all_Resource();
		if (!result)
		{
			MessageBox(hWnd, _T("位图加载失败"), L"Error", MB_OK);
			DestroyWindow(hWnd);
		}
		Set_CurScene(STATUS::Main);
		LoadOption();
		{
			RenderManager::Instance()->getRenderer()->SetRenderTarget(pRenderTarget);
			thread T(&Render_Thread,
				RenderManager::Instance()->getRenderer(),
				RenderManager::Instance()->getfpsController(),
				RenderManager::Instance()->getBufferManager()
			);
			T.detach();
		}
		SetTimer(hWnd, sceneTick, 20, NULL);
		PostMessage(hWnd, WM_COMMAND, IDB_LOCALGAME, (LPARAM)hWnd);
		break;
	}

	case WM_TIMER:
	{
		switch (wParam)
		{
		case hall_refreash:
			REQUESTMANAGER->RequestLobbyUserData();
			REQUESTMANAGER->RequestLobbyRoomData();
			break;
		case room_refreash:
			REQUESTMANAGER->RequestRoomInfo();
			break;
		case sceneTick:
			_Scene::CurScene->Tick();
			break;
		}
		InvalidateRect(hWnd, NULL, TRUE);
		break;
	}

	case WM_ERASEBKGND:
	{
		return true;
		break;
	}

	case WM_KILLFOCUS:
	{
		FocusManager::Instance()->SetFocus(false);
		MousePos::MoveX = -1;
		MousePos::MoveY = -1;
		MouseManager::Instance()->ReSet();
		break;
	}

	case WM_SETFOCUS:
	{
		FocusManager::Instance()->SetFocus(true);
		break;
	}

	case WM_ACTIVATE:
	{
		// LOWORD(wParam) :
		// WA_ACTIVE - 通过非鼠标方式激活
		// WA_CLICKACTIVE - 通过鼠标点击激活  
		// WA_INACTIVE - 被停用
		bool active = (LOWORD(wParam) != WA_INACTIVE);
		break;
	}

	case WM_ACTIVATEAPP:
	{
		bool active = wParam != 0;
		FocusManager::Instance()->SetActive(active);
		break;
	}

	case WM_COMMAND:
	{
		int wmId = LOWORD(wParam);
		int wmEvent = HIWORD(wParam);
		switch (Get_CurScene())
		{
		case STATUS::Main:
		{
			switch (wmId)
			{
				// 开始游戏
			case IDB_LOCALGAME: {
				BusyLoaderDialog dialog;
				if (dialog.Create(RENDERCONTEXMANAGER->GetHwnd(), RENDERCONTEXMANAGER->GetHinstance()))
				{
					auto task = CoroTask::Run([]()->bool {
						WaitForInitOpenGL();
						RENDERCONTEXMANAGER->GetThreadRenderContext()->Bind();
						ResFactory->InitOpenGLResource();
						RENDERCONTEXMANAGER->GetThreadRenderContext()->Release();
						GameWorldManager::Instance()->InitGameWorld(GameMode::RunGame, 1);
						return true;
						});

					dialog.Show(std::move(task), nullptr, "初始化游戏......");

					auto result = dialog.WaitResultAndClose();
					if (result == DialogResult::SUCCESSED)
					{
						ShowCursor(FALSE);
						if (auto input = GameWorldManager::Instance()->GetGameWorld()->getSystem<LocalInputSystem>())
							input->setEnabled(false);
						GameWorldManager::Instance()->RunWorld();
						CoroTask::Run([]()->Task<void> {
							co_await CoSleep(200ms);
							if (auto input = GameWorldManager::Instance()->GetGameWorld()->getSystem<LocalInputSystem>())
								input->setEnabled(true);
							});
						Set_CurScene(STATUS::LocalGame_Status);
						InvalidateRect(hWnd, NULL, TRUE);
					}
					else if (result == DialogResult::FAILED)
					{
						MessageBox(RENDERCONTEXMANAGER->GetHwnd(), L"游戏初始化失败！", TEXT("开始游戏"), MB_OK);
					}
				}
				break;
			}
							  // 联机大厅
			case IDB_ENTERHALL: {
				if (!DialogBox(RENDERCONTEXMANAGER->GetHinstance(), MAKEINTRESOURCE(IDD_DIALOG_Userid), hWnd, (DLGPROC)GetID_Proc))
					break;

				BusyLoaderDialog dialog;
				if (dialog.Create(RENDERCONTEXMANAGER->GetHwnd(), RENDERCONTEXMANAGER->GetHinstance()))
				{
					auto task = ConnectManager::Instance()->Login();

					dialog.Show(std::move(task), nullptr, "登录中......");

					auto result = dialog.WaitResultAndClose();
					if (result == DialogResult::SUCCESSED)
					{
						Set_CurScene(STATUS::Hall_Status);
						SetTimer(hWnd, hall_refreash, 3000, NULL);
						UpdateWindow(hWnd);
					}
					else if (result == DialogResult::FAILED)
					{
						MessageBox(RENDERCONTEXMANAGER->GetHwnd(), L"登录失败，无法连接到服务器或者非合规用户名！", TEXT("联机大厅"), MB_OK);
					}
				}
				break;
			}
							  // 设置
			case IDB_OPTION: {
				Set_CurScene(STATUS::Option);
				break;
			}
						   //地图编辑
			case IDB_MAPEDIT: {
				//Set_CurScene(STATUS::MapEdit);
				break;
			}
							// 退出游戏
			case IDB_QUITGAME:
			{
				DestroyWindow(hWnd);
				break;
			}
			}
			break;
		}
		case STATUS::Option:
		{
			switch (wmId)
			{
			case IDB_EXITOPTION:
			{
				Set_CurScene(STATUS::Main);
				break;
			}
			case IDB_SETFPS_30:
			{
				int frame = 30;
				RenderManager::Instance()->getfpsController()->setGameTargetFps(frame);
				WritePrivateProfileString(L"Video", L"FPS", std::to_wstring(frame).c_str(), L"./option.ini");
				break;
			}
			case IDB_SETFPS_60:
			{
				int frame = 60;
				RenderManager::Instance()->getfpsController()->setGameTargetFps(frame);
				WritePrivateProfileString(L"Video", L"FPS", std::to_wstring(frame).c_str(), L"./option.ini");
				break;
			}
			case IDB_SETFPS_144:
			{
				int frame = 144;
				RenderManager::Instance()->getfpsController()->setGameTargetFps(frame);
				WritePrivateProfileString(L"Video", L"FPS", std::to_wstring(frame).c_str(), L"./option.ini");
				break;
			}
			default:
				break;
			}

		}
		case STATUS::Hall_Status:
		{
			switch (wmId)
			{
			case Enterroom:
			{
				KillTimer(hWnd, hall_refreash);
				SetTimer(hWnd, room_refreash, 1000, NULL);
				Set_CurScene(STATUS::Room_Status);
				UpdateWindow(RENDERCONTEXMANAGER->GetHwnd());
				break;
			}
			// 刷新
			case IDB_REFRESH:
			{
				REQUESTMANAGER->RequestLobbyUserData();
				REQUESTMANAGER->RequestLobbyRoomData();
				break;
			}
			// 加入房间
			case IDB_ENTERROOM:
			{
				if (auto selectRoom = LOBBYMANAGER->GetSelectRoom())
				{
					BusyLoaderDialog dialog;
					if (dialog.Create(RENDERCONTEXMANAGER->GetHwnd(), RENDERCONTEXMANAGER->GetHinstance()))
					{
						auto task = LOBBYMANAGER->TryJoinRoom(selectRoom);

						dialog.Show(std::move(task), nullptr, "加入房间中......");

						auto result = dialog.WaitResultAndClose();
						if (result == DialogResult::SUCCESSED)
						{
							SendMessage(RENDERCONTEXMANAGER->GetHwnd(), WM_COMMAND, Enterroom, (LPARAM)RENDERCONTEXMANAGER->GetHwnd());
						}
						else if (result == DialogResult::FAILED)
						{
							MessageBox(RENDERCONTEXMANAGER->GetHwnd(), L"状态异常！", NULL, MB_OK);
						}
					}
				}
				break;
			}
			// 离开大厅
			case IDB_EXITHALL:
			{
				BusyLoaderDialog dialog;
				if (dialog.Create(RENDERCONTEXMANAGER->GetHwnd(), RENDERCONTEXMANAGER->GetHinstance()))
				{
					auto task = []()->Task<bool> {
						co_await ConnectManager::Instance()->Logout();
						co_return true;
						}();

					dialog.Show(std::move(task), nullptr, "离开大厅......");

					auto result = dialog.WaitResultAndClose();
					KillTimer(hWnd, hall_refreash);
					Set_CurScene(STATUS::Main);
					UpdateWindow(RENDERCONTEXMANAGER->GetHwnd());
				}
				break;
			}
			case IDB_HALL_SEND:
			{
				wchar_t temp[1024] = { '\0' };
				GetWindowTextW(_Scene::SHall->Hall_edit_in, temp, 1024);
				wstring str = temp;
				if (!str.empty())
				{
					REQUESTMANAGER->SendHallMsg(Tool::WStringToUTF8(str));
					SendMessage(_Scene::SHall->Hall_edit_in, WM_SETTEXT, 0, (LPARAM)L"");
				}
				break;
			}
			// 创建房间
			case IDB_CREATEROOM:
			{
				BusyLoaderDialog dialog;
				if (dialog.Create(RENDERCONTEXMANAGER->GetHwnd(), RENDERCONTEXMANAGER->GetHinstance()))
				{
					auto task = LOBBYMANAGER->TryCreateRoom();

					dialog.Show(std::move(task), nullptr, "创建房间......");

					auto result = dialog.WaitResultAndClose();
					if (result == DialogResult::SUCCESSED)
					{
						REQUESTMANAGER->RequestRoomInfo();
						Set_CurScene(STATUS::Room_Status);
						KillTimer(hWnd, hall_refreash);
						SetTimer(hWnd, room_refreash, 1000, NULL);
						UpdateWindow(RENDERCONTEXMANAGER->GetHwnd());
					}
					else if (result == DialogResult::FAILED)
					{
						MessageBox(RENDERCONTEXMANAGER->GetHwnd(), L"状态异常！", NULL, MB_OK);
					}
				}
				break;
			}
			case HALL_ROOM_LIST:
			{
				if (wmEvent == LBN_DBLCLK)
				{
					if (auto selectRoom = LOBBYMANAGER->GetSelectRoom())
					{
						BusyLoaderDialog dialog;
						if (dialog.Create(RENDERCONTEXMANAGER->GetHwnd(), RENDERCONTEXMANAGER->GetHinstance()))
						{
							auto task = LOBBYMANAGER->TryJoinRoom(selectRoom);

							dialog.Show(std::move(task), nullptr, "加入房间中......");

							auto result = dialog.WaitResultAndClose();
							if (result == DialogResult::SUCCESSED)
							{
								SendMessage(RENDERCONTEXMANAGER->GetHwnd(), WM_COMMAND, Enterroom, (LPARAM)RENDERCONTEXMANAGER->GetHwnd());
							}
							else if (result == DialogResult::FAILED)
							{
								MessageBox(RENDERCONTEXMANAGER->GetHwnd(), L"状态异常！", NULL, MB_OK);
							}
						}
					}
				}
				break;
			}
			}
			break;
		}
		case STATUS::Room_Status:
		{
			switch (wmId)
			{
			case IDB_STARTGAME:
			{
				if (!LOBBYMANAGER->IsHost())
					break;

				BusyLoaderDialog dialog;
				if (dialog.Create(RENDERCONTEXMANAGER->GetHwnd(), RENDERCONTEXMANAGER->GetHinstance()))
				{
					auto task = LOBBYMANAGER->TryStartGame();

					dialog.Show(std::move(task), nullptr, "开始游戏......");

					auto result = dialog.WaitResultAndClose();
					if (result == DialogResult::SUCCESSED)
					{
					}
					else if (result == DialogResult::FAILED)
					{
						MessageBox(RENDERCONTEXMANAGER->GetHwnd(), L"存在未准备的玩家！", NULL, MB_OK);
					}
				}
				break;
			}
			case IDB_READY:
			{
				BusyLoaderDialog dialog;
				if (dialog.Create(RENDERCONTEXMANAGER->GetHwnd(), RENDERCONTEXMANAGER->GetHinstance()))
				{
					auto task = LOBBYMANAGER->TryChangeReadyStatus(true);

					dialog.Show(std::move(task), nullptr, "取消准备......");

					auto result = dialog.WaitResultAndClose();
					if (result == DialogResult::SUCCESSED)
					{
						REQUESTMANAGER->RequestRoomInfo();
						_Scene::SRoom->ModifyButton_ID(IDB_READY, IDB_CANCELREADY);
						_Scene::SRoom->ModifyButton_Text(IDB_CANCELREADY, L"取消准备");
					}
					else if (result == DialogResult::FAILED)
					{
						MessageBox(RENDERCONTEXMANAGER->GetHwnd(), L"状态异常！", NULL, MB_OK);
					}
				}
				break;
			}
			case IDB_CANCELREADY:
			{
				BusyLoaderDialog dialog;
				if (dialog.Create(RENDERCONTEXMANAGER->GetHwnd(), RENDERCONTEXMANAGER->GetHinstance()))
				{
					auto task = LOBBYMANAGER->TryChangeReadyStatus(false);

					dialog.Show(std::move(task), nullptr, "取消准备......");

					auto result = dialog.WaitResultAndClose();
					if (result == DialogResult::SUCCESSED)
					{
						REQUESTMANAGER->RequestRoomInfo();
						_Scene::SRoom->ModifyButton_ID(IDB_CANCELREADY, IDB_READY);
						_Scene::SRoom->ModifyButton_Text(IDB_READY, L"准备");
					}
					else if (result == DialogResult::FAILED)
					{
						MessageBox(RENDERCONTEXMANAGER->GetHwnd(), L"状态异常！", NULL, MB_OK);
					}
				}
				break;
			}
			case DISBANDINROOM:
			{
				KillTimer(hWnd, room_refreash);
				MessageBox(RENDERCONTEXMANAGER->GetHwnd(), L"房主已将房间解散！", NULL, MB_OK);
				Set_CurScene(STATUS::Hall_Status);
				SetTimer(hWnd, hall_refreash, 4000, NULL);
				UpdateWindow(hWnd);
				break;
			}
			case IDB_EXITROOM:
			{
				BusyLoaderDialog dialog;
				if (dialog.Create(RENDERCONTEXMANAGER->GetHwnd(), RENDERCONTEXMANAGER->GetHinstance()))
				{
					auto task = LOBBYMANAGER->TryLeaveRoom();

					dialog.Show(std::move(task), nullptr, "取消准备......");

					auto result = dialog.WaitResultAndClose();
					if (result == DialogResult::SUCCESSED)
					{
						KillTimer(hWnd, room_refreash);
						Set_CurScene(STATUS::Hall_Status);
						SetTimer(hWnd, hall_refreash, 4000, NULL);
						UpdateWindow(hWnd);
					}
					else if (result == DialogResult::FAILED)
					{
						MessageBox(RENDERCONTEXMANAGER->GetHwnd(), L"状态异常！", NULL, MB_OK);
					}
				}
				break;
			}
			case START:
			{
				KillTimer(hWnd, room_refreash);

				GameWorldManager::Instance()->InitOnlineGameWorld();
				GameWorldManager::Instance()->RunWorld();
				Set_CurScene(STATUS::OnlineGame_Status);

				break;
			}
			case IDB_ROOM_SEND:
			{
				wchar_t temp[1024] = { '\0' };
				GetWindowTextW(_Scene::SRoom->Room_edit_in, temp, 1024);
				wstring str = temp;
				if (!str.empty())
				{
					REQUESTMANAGER->SendRoomMsg(Tool::WStringToUTF8(str));
					SendMessage(_Scene::SRoom->Room_edit_in, WM_SETTEXT, 0, (LPARAM)L"");
				}
				break;
			}
			}
			break;
		}
		case STATUS::LocalGame_Status:
		{
			switch (wmId)
			{
				// 暂停
			case IDB_PAUSE:
			{
				GameWorldManager::Instance()->PauseWorld();
				int i = DialogBox(RENDERCONTEXMANAGER->GetHinstance(), MAKEINTRESOURCE(IDD_DIALOG_PAUSE), hWnd, Pause);
				switch (i) {
				case 10:	//回到游戏
					GameWorldManager::Instance()->RunWorld();
					break;
				case 20:	//重新开始
					GameWorldManager::Instance()->StopWorld();
					GameWorldManager::Instance()->InitGameWorld(GameMode::RunGame, 1);
					GameWorldManager::Instance()->RunWorld();
					break;
				case 30:	//回到主菜单
					GameWorldManager::Instance()->StopWorld();
					Set_CurScene(STATUS::Main);
					break;
				}
				break;
			}
			case ReLoadMap:
			{
				//Game::Instance()->LoadLocalMap();
				break;
			}
			}
			break;
		}
		case STATUS::OnlineGame_Status:
		{
			switch (wmId)
			{
			case IDB_RETURN:
			{
				int i = DialogBox(RENDERCONTEXMANAGER->GetHinstance(), MAKEINTRESOURCE(IDD_DIALOG_RETURN), hWnd, Return);
				switch (i) {
				case 10:	//回到游戏
					break;
				case 20:	//退出游戏
					if (REQUESTMANAGER->RequestLeaveGame().sync_wait())
					{
						GameWorldManager::Instance()->StopWorld();
						Set_CurScene(STATUS::Hall_Status);
						SetTimer(hWnd, hall_refreash, 4000, NULL);
					}
					break;
					//case 30:	//回到主菜单
					//	GameWorldManager::Instance()->StopWorld();
					//	Set_CurScene(STATUS::Main);
					//	break;
				}
				break;
			}
			case WIN:
			{
				CONNECTMANAGER->LogoutGameSeervice();
				_Scene::CurScene = _Scene::SWinGame;
				break;
			}
			case FAIL:
			{
				CONNECTMANAGER->LogoutGameSeervice();
				_Scene::CurScene = _Scene::SFailGame;
				break;
			}
			case ReturnInEndGame:
			{
				SetTimer(hWnd, room_refreash, 1000, NULL);
				Set_CurScene(STATUS::Room_Status);
				_Scene::SRoom->Show(true);
				break;
			}
			}
		}
		default:
		{
			switch (wmId)
			{
			case IDM_EXIT:
				SafeRelease(pRenderTarget);
				SafeRelease(pD2DFactory);
				DestroyWindow(hWnd);
				break;
			default:
				return DefWindowProc(hWnd, message, wParam, lParam);
			}
			break;
		}
		}
	}

	case WM_HSCROLL:
	{
		HWND hScrollBar = (HWND)lParam;
		int ctrlId = GetDlgCtrlID(hScrollBar);

		switch (ctrlId) {
		case Scene_Option::ID_SLIDER_MASTERVOL:
		{
			UINT pos = (int)SendMessage(hScrollBar, TBM_GETPOS, 0, 0);
			pos = std::clamp(pos, (UINT)0, (UINT)100);
			float volume = float(pos) / 100.0f;
			if (auto device = AudioDeviceManager::Instance()->GetDevice())
				device->SetMasterVolumn(volume);

			if (_Scene::SOption)
				_Scene::SOption->UpdateVolumeText(Scene_Option::ID_TEXT_MASTERVOL, pos);

			WritePrivateProfileString(L"Audio", L"MasterVolume", std::to_wstring(pos).c_str(), L"./option.ini");
			break;
		}

		case Scene_Option::ID_SLIDER_MUSIC:
		{
			UINT pos = (int)SendMessage(hScrollBar, TBM_GETPOS, 0, 0);
			pos = std::clamp(pos, (UINT)0, (UINT)100);
			float volume = float(pos) / 100.0f;
			if (auto device = AudioDeviceManager::Instance()->GetDevice())
				device->SetChannelVolumn((AudioChannelID)AudioChannelDef::BGM_Channel, volume);

			if (_Scene::SOption)
				_Scene::SOption->UpdateVolumeText(Scene_Option::ID_TEXT_MUSIC, pos);

			WritePrivateProfileString(L"Audio", L"MusicVolume", std::to_wstring(pos).c_str(), L"./option.ini");
			break;
		}

		case Scene_Option::ID_SLIDER_SFX:    // 2002
		{
			UINT pos = (int)SendMessage(hScrollBar, TBM_GETPOS, 0, 0);
			pos = std::clamp(pos, (UINT)0, (UINT)100);
			float volume = float(pos) / 100.0f;
			if (auto device = AudioDeviceManager::Instance()->GetDevice())
				device->SetChannelVolumn((AudioChannelID)AudioChannelDef::SoundEffects_Channel, volume);

			if (_Scene::SOption)
				_Scene::SOption->UpdateVolumeText(Scene_Option::ID_TEXT_SFX, pos);

			WritePrivateProfileString(L"Audio", L"SFXVolume", std::to_wstring(pos).c_str(), L"./option.ini");
			break;
		}
		}
		break;
	}

	case WM_MOUSEMOVE:
	{
		if (FocusManager::Instance()->ShouldProcessInput())
		{
			int mouseX = LOWORD(lParam), mouseY = HIWORD(lParam);
			int lastMouseX = MousePos::MoveX, lastMouseY = MousePos::MoveY;

			MousePos::MoveX = mouseX;
			MousePos::MoveY = mouseY;
			if (_Scene::CurScene)
				_Scene::CurScene->Move();

			auto status = Get_CurScene();
			if (status == STATUS::LocalGame_Status || status == STATUS::OnlineGame_Status)
			{
				RECT main_rect = RENDERCONTEXMANAGER->GetRECT();
				POINT center = { main_rect.right / 2, main_rect.bottom / 2 };
				ClientToScreen(RENDERCONTEXMANAGER->GetHwnd(), &center);
				SetCursorPos(center.x, center.y);

				if (lastMouseX != -1 && lastMouseX != -1)
					MouseManager::Instance()->ReSet(main_rect.right / 2, main_rect.bottom / 2);
			}
			MouseManager::Instance()->InputPos(MousePos::MoveX, MousePos::MoveY);

			InvalidateRect(hWnd, NULL, TRUE);
		}
		break;
	}

	case WM_LBUTTONDOWN:
	{
		if (!FocusManager::Instance()->ShouldProcessInput())
			break;

		MousePos::ClickX = LOWORD(lParam);
		MousePos::ClickY = HIWORD(lParam);
		if (_Scene::CurScene)
			_Scene::CurScene->Click(true, GetAsyncKeyState(VK_SHIFT) & 0x8000);
		InvalidateRect(hWnd, NULL, TRUE);
		break;
	}

	case WM_RBUTTONDOWN:
	{
		if (!FocusManager::Instance()->ShouldProcessInput())
			break;

		MousePos::ClickX = LOWORD(lParam);
		MousePos::ClickY = HIWORD(lParam);
		if (_Scene::CurScene)
			_Scene::CurScene->Click(false, GetAsyncKeyState(VK_SHIFT) & 0x8000);
		InvalidateRect(hWnd, NULL, TRUE);
		break;
	}

	case WM_DESTROY:
	{
		PostQuitMessage(0);
		break;
	}
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

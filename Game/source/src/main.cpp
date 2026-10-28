
#include "stdafx.h"
#include "RenderProcess.h"
#include "Manager/ResourceManager.h"

#include "GameEngine.h"
#include "Input/EngineKey.h"

#include "GeneralManager/RenderManager.h"
#include "GeneralManager/FocusManager.h"
#include "GeneralManager/WindowHandleManager.h"

#include "BusyLoaderDialog.h"

#define MAX_LOADSTRING 100

WCHAR szTitle[MAX_LOADSTRING] = L"HyTestGame";  // 标题栏文本
WCHAR szWindowClass[MAX_LOADSTRING];            // 主窗口类名


bool Init_all_Resource()
{
	LOGINFO("Init", "Init_All");
	ResFactory->InitD2DResource();
	ResFactory->InitAudioResource();
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
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_Game));
	wcex.hCursor = LoadCursor(hInstance, MAKEINTRESOURCE(MYCURSOR));
	wcex.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_Game));

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
	WINDOWHANDLEMANAGER->SetHinstance(hInstance);
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

enum { sceneTick };	//定时器ID

namespace
{
	// 一个 UTF-16 码元转 UTF-8。WM_CHAR 送来的合成字符都在 BMP 以内；
	// 代理对（emoji 之类）会被拆成两次 WM_CHAR，这里不拼，先按两段分别推送。
	std::string Utf8FromCodeUnit(unsigned int code)
	{
		std::string out;
		if (code < 0x80)
		{
			out.push_back(static_cast<char>(code));
		}
		else if (code < 0x800)
		{
			out.push_back(static_cast<char>(0xC0 | (code >> 6)));
			out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
		}
		else
		{
			out.push_back(static_cast<char>(0xE0 | (code >> 12)));
			out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
		}
		return out;
	}

	void SyncInputViewport(HWND hWnd)
	{
		auto input = GameEngine::Instance().Input();
		if (!input)
			return;

		RECT rect{};
		if (!GetClientRect(hWnd, &rect))
			return;

		input->SetViewportRect(0.0f, 0.0f,
			static_cast<float>(rect.right - rect.left),
			static_cast<float>(rect.bottom - rect.top));
	}
}

namespace MousePosRecord
{
	int MoveX, MoveY, ClickX, ClickY;
}

std::shared_ptr<std::thread> renderThread;
bool shouldStopRenderThread = false;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_CREATE:
	{
		if (ShowConsole)
			CreateDebugConsole();

		RECT rect;
		GetClientRect(hWnd, &rect);
		WINDOWHANDLEMANAGER->SetHwnd(hWnd);
		WINDOWHANDLEMANAGER->SetRect(rect);


		std::string engineError;
		if (!GameEngine::Instance().Initialize(&engineError))
		{
			MessageBoxA(hWnd, engineError.c_str(), "引擎初始化失败", MB_OK);
			DestroyWindow(hWnd);
			break;
		}

		std::string openProjectError;
		if (!GameEngine::Instance().Runtime().OpenProject("D:/Desktop/code/HYEngine/bin/temp/project2/333333.project", &openProjectError))
		{
			MessageBoxA(hWnd, openProjectError.c_str(), "项目初始化失败", MB_OK);
			DestroyWindow(hWnd);
			break;
		}

		std::string renderError;
		if (!GameEngine::Instance().CreateRenderer(static_cast<uint32_t>(rect.right - rect.left), static_cast<uint32_t>(rect.bottom - rect.top), &renderError))
		{
			MessageBoxA(hWnd, renderError.c_str(), "渲染初始化失败", MB_OK);
			DestroyWindow(hWnd);
			break;
		}

		SyncInputViewport(hWnd);

		bool result = Init_all_Resource();
		if (!result)
		{
			MessageBox(hWnd, _T("位图加载失败"), L"Error", MB_OK);
			DestroyWindow(hWnd);
		}

		renderThread = std::make_shared<std::thread>(&RenderLoop, &shouldStopRenderThread);

		SetTimer(hWnd, sceneTick, 6, NULL);

		BusyLoaderDialog dialog;
		if (dialog.Create(WINDOWHANDLEMANAGER->GetHwnd(), WINDOWHANDLEMANAGER->GetHinstance()))
		{
			auto task = CoroTask::Run([]()->bool {
				WaitForInitScene3D();
				auto projectManager = GameEngine::Instance().Runtime().GetProjectManager();
				auto guid = projectManager->GetDefaultSceneGUID();
				if (guid.empty() || !GameEngine::Instance().Runtime().OpenScene(guid))
					GameEngine::Instance().Runtime().OpenEmptyScene(nullptr);
				return true;
				});

			dialog.Show(std::move(task), nullptr, "初始化游戏......");

			auto result = dialog.WaitResultAndClose();
			if (result == DialogResult::SUCCESSED)
			{
				//ShowCursor(FALSE);
				GameEngine::Instance().StartSceneThread(1000.f / 165.f, 1000.f / 60.f);
				InvalidateRect(hWnd, NULL, TRUE);
			}
			else if (result == DialogResult::FAILED)
			{
				MessageBox(WINDOWHANDLEMANAGER->GetHwnd(), L"游戏初始化失败！", TEXT("开始游戏"), MB_OK);
			}
		}

		break;
	}

	case WM_TIMER:
	{
		switch (wParam)
		{
		case sceneTick:
			if (auto input = GameEngine::Instance().Input())
				input->NewFrame();

			auto& engineRuntime = GameEngine::Instance().Runtime();

			if (engineRuntime.TakeQuitRequest())
			{
				DestroyWindow(hWnd);
				return 0;
			}

			if (engineRuntime.HasPendingSceneChange())
			{
				std::string error;
				if (!GameEngine::Instance().Runtime().PumpSceneChanges(&error) && error.empty())
				{
				}
			}

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

	case WM_KEYDOWN:
	case WM_KEYUP:
	{
		if (auto input = GameEngine::Instance().Input())
			input->SetKeyDown(EngineKeyFromVirtualKey(int(wParam)), message == WM_KEYDOWN);
		break;
	}

	case WM_CHAR:
	{
		if (auto input = GameEngine::Instance().Input())
		{
			const unsigned int code = static_cast<unsigned int>(wParam);
			if (code >= 0x20 && code != 0x7F)
				input->PushTextInput(Utf8FromCodeUnit(code));
		}
		break;
	}

	case WM_SIZE:
	{
		SyncInputViewport(hWnd);
		break;
	}

	case WM_MOUSEWHEEL:
	{
		if (auto input = GameEngine::Instance().Input())
			input->PushMouseWheel(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA));
		break;
	}

	case WM_KILLFOCUS:
	{
		FocusManager::Instance()->SetFocus(false);
		if (auto input = GameEngine::Instance().Input())
		{
			input->SetActive(false);
			input->ReleaseAllKeys();
		}
		break;
	}

	case WM_SETFOCUS:
	{
		FocusManager::Instance()->SetFocus(true);
		if (auto input = GameEngine::Instance().Input())
		{
			input->SetActive(true);
			input->ReleaseAllKeys();
		}
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
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	case WM_MOUSEMOVE:
	{
		if (auto input = GameEngine::Instance().Input())
			input->SetMousePosition(static_cast<float>(LOWORD(lParam)), static_cast<float>(HIWORD(lParam)));

		if (FocusManager::Instance()->ShouldProcessInput())
		{
			int mouseX = LOWORD(lParam), mouseY = HIWORD(lParam);
			const int lastMouseX = MousePosRecord::MoveX, lastMouseY = MousePosRecord::MoveY;
			if (lastMouseX >= 0 && lastMouseY >= 0)
			{
				if (auto input = GameEngine::Instance().Input())
				{
					input->AddMouseDelta(float(mouseX - lastMouseX), float(mouseY - lastMouseY));
					input->SetMousePosition(float(mouseX), float(mouseY));
				}
			}

			MousePosRecord::MoveX = mouseX;
			MousePosRecord::MoveY = mouseY;

			//if (inGame)
			//{
			//	RECT main_rect = WINDOWHANDLEMANAGER->GetRect();
			//	const int centerClientX = main_rect.right / 2;
			//	const int centerClientY = main_rect.bottom / 2;
			//	POINT center = { centerClientX, centerClientY };
			//	ClientToScreen(WINDOWHANDLEMANAGER->GetHwnd(), &center);
			//	SetCursorPos(center.x, center.y);

			//	// 光标被拉回中心，下一次位移从中心开始算。
			//	MousePos::MoveX = centerClientX;
			//	MousePos::MoveY = centerClientY;
			//}

			InvalidateRect(hWnd, NULL, TRUE);
		}
		break;
	}

	case WM_LBUTTONDOWN:
	{
		if (auto input = GameEngine::Instance().Input())
			input->SetKeyDown(EngineKey::MouseLeft, true);

		if (!FocusManager::Instance()->ShouldProcessInput())
			break;

		MousePosRecord::ClickX = LOWORD(lParam);
		MousePosRecord::ClickY = HIWORD(lParam);

		InvalidateRect(hWnd, NULL, TRUE);
		break;
	}

	case WM_LBUTTONUP:
	{
		if (auto input = GameEngine::Instance().Input())
			input->SetKeyDown(EngineKey::MouseLeft, false);
		break;
	}

	case WM_RBUTTONDOWN:
	{
		if (auto input = GameEngine::Instance().Input())
			input->SetKeyDown(EngineKey::MouseRight, true);

		if (!FocusManager::Instance()->ShouldProcessInput())
			break;

		MousePosRecord::ClickX = LOWORD(lParam);
		MousePosRecord::ClickY = HIWORD(lParam);

		InvalidateRect(hWnd, NULL, TRUE);
		break;
	}

	case WM_RBUTTONUP:
	{
		if (auto input = GameEngine::Instance().Input())
			input->SetKeyDown(EngineKey::MouseRight, false);
		break;
	}

	case WM_MBUTTONDOWN:
	{
		if (auto input = GameEngine::Instance().Input())
			input->SetKeyDown(EngineKey::MouseMiddle, true);
		break;
	}

	case WM_MBUTTONUP:
	{
		if (auto input = GameEngine::Instance().Input())
			input->SetKeyDown(EngineKey::MouseMiddle, false);
		break;
	}

	case WM_DESTROY:
	{
		shouldStopRenderThread = true;
		renderThread->join();
		GameEngine::Instance().StopSceneThread();
		GameEngine::Instance().CloseScene();
		GameEngine::Instance().Shutdown();
		PostQuitMessage(0);
		break;
	}
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

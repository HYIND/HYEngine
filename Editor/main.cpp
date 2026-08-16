// Editor.cpp : 定义应用程序的入口点。
//
#include "stdafx.h"
#include "framework.h"
#include "Editor.h"

#include <iostream>

#include "ImguiLayout.h"

#include <stdio.h>
#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

#include "WorldManager.h"

#include <io.h>
bool ShowConsole = false;
bool CreateDebugConsole(const wchar_t* title = L"Editor Debug Console") {
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

static void glfw_error_callback(int error, const char* description)
{
	fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

#include "Helper/Tools.h"
#include <filesystem>
namespace fs = std::filesystem;
static void glfw_filedrop_callback(GLFWwindow* window, int count, const char* paths[])
{

	double mouseX, mouseY;
	glfwGetCursorPos(window, &mouseX, &mouseY);
	int winX, winY;
	glfwGetWindowPos(window, &winX, &winY);

	ImVec2 mousePos((float)mouseX + winX, (float)mouseY + winY);

	for (int i = 0; i < count; i++)
	{
		std::string path = paths[i];
		if (!fs::path(path).is_absolute())
			continue;
		if (fs::is_directory(fs::path(path)))
			ImguiLayout::DropFolder(path, ProjectManager::Get(), mousePos);
		if (fs::is_regular_file(fs::path(path)))
			ImguiLayout::DropFile(path, ProjectManager::Get(), mousePos);
	}
}

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

	if (ShowConsole)
		CreateDebugConsole();

	glfwSetErrorCallback(glfw_error_callback);
	if (!glfwInit())
		return 1;

	// Select GL version + let the backend select a GLSL version
	const char* glsl_version = nullptr;

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);

	// Create window with graphics context
	float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor()); // Valid on GLFW 3.3+ only
	GLFWmonitor* monitor = glfwGetPrimaryMonitor();
	int workAreaX, workAreaY, workAreaWidth, workAreaHeight;
	glfwGetMonitorWorkarea(monitor, &workAreaX, &workAreaY, &workAreaWidth, &workAreaHeight);
	GLFWwindow* window = glfwCreateWindow((int)(workAreaWidth * 0.8f * main_scale), (int)(workAreaHeight * 0.8f * main_scale), "Editor", nullptr, nullptr);
	if (window == nullptr)
		return 1;
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1); // Enable vsync

	glfwSetDropCallback(window, glfw_filedrop_callback);

	glewExperimental = GL_TRUE;
	GLenum err = glewInit();
	if (err != GLEW_OK) {
		std::cerr << "Failed to initialize GLEW: " << glewGetErrorString(err) << std::endl;
		return -1;
	}

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
	//io.ConfigViewportsNoAutoMerge = true;
	//io.ConfigViewportsNoTaskBarIcon = true;


	// 微软雅黑路径
	const char* fontPath = "C:/Windows/Fonts/msyh.ttc";

	// 字体配置
	ImFontConfig fontConfig;
	fontConfig.OversampleH = 2;
	fontConfig.OversampleV = 2;
	fontConfig.PixelSnapH = true;

	// 使用简体中文常用字范围
	static const ImWchar ranges[] = {
		0x0020, 0x00FF,  // 基本拉丁字母 + 标点
		0x4E00, 0x9FA5,  // 常用汉字（CJK统一表意文字）
		0
	};

	// 尝试加载字体
	ImFont* font = io.Fonts->AddFontFromFileTTF(
		fontPath,
		18.0f * main_scale,  // 根据 DPI 缩放
		&fontConfig,
		ranges  // 或用 io.Fonts->GetGlyphRangesChineseSimplifiedCommon()
	);

	if (font == nullptr) {
		io.Fonts->AddFontDefault();
		if (ShowConsole)
			std::cout << "警告：加载微软雅黑失败，使用默认字体\n";
	}
	else {
		io.FontDefault = font;
		if (ShowConsole)
			std::cout << "微软雅黑字体加载成功！\n";
	}

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsLight();

	// Setup scaling
	ImGuiStyle& style = ImGui::GetStyle();
	style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
	style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)
	io.ConfigDpiScaleFonts = true;          // [Experimental] Automatically overwrite style.FontScaleDpi in Begin() when Monitor DPI changes. This will scale fonts but _NOT_ scale sizes/padding for now.
	io.ConfigDpiScaleViewports = true;      // [Experimental] Scale Dear ImGui and Platform Windows when Monitor DPI changes.

	// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init(glsl_version);

	ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
	ImGuizmo::Enable(true);

	{
		auto hwnd = glfwGetWin32Window(window);
		auto hdc = wglGetCurrentDC();
		auto hglrc = wglGetCurrentContext();

		RENDERCONTEXMANAGER->SetHwnd(hwnd);
		RENDERCONTEXMANAGER->SetHGLRC(hglrc);

		{
			auto guard = THREADCONTEXT->GetBindGuard();

			WorldManager::Instance()->InitWorld();
			WorldManager::Instance()->RunWorld();
			//WorldManager::Instance()->PauseWorld();
		}

		wglMakeCurrent(hdc, hglrc);
	}

	bool show_demo_window = true;
	bool show_another_window = false;
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	// 在主循环中
	static bool firstFrame = true;

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
		if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
		{
			ImGui_ImplGlfw_Sleep(10);
			continue;
		}

		// 开始 ImGui 帧
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		ImguiLayout::DrawMainMenu(window, ProjectManager::Get());

		// ============ 创建 Dockspace（直接在主窗口内容区域） ============
		// 获取主视口
		ImguiLayout::SetDockspace(firstFrame);
		if (firstFrame)
			firstFrame = false;

		// ============ 创建各个窗口（会自动停靠到 Dockspace） ============

		// 场景物体窗口 (左侧)
		ImguiLayout::DrawSceneObjectList(WorldManager::Instance());

		// 场景物体窗口 (左侧)
		ImguiLayout::DrawOptions(WorldManager::Instance());

		// 属性面板 (右侧)
		ImguiLayout::DrawObjectProperties();

		// 资产管理 (底部)
		ImguiLayout::DrawAssetBrowser(ProjectManager::Get());

		// 底部状态栏
		ImguiLayout::DrawStatusBar(WorldManager::Instance());

		// 场景视图 (中间)
		ImguiLayout::DrawSceneView(WorldManager::Instance(), ProjectManager::Get());

		// ============ 渲染 ============
		ImGui::Render();

		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(clear_color.x * clear_color.w,
			clear_color.y * clear_color.w,
			clear_color.z * clear_color.w,
			clear_color.w);
		glClear(GL_COLOR_BUFFER_BIT);

		// 渲染 ImGui
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		// 更新多视口
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			GLFWwindow* backup_current_context = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(backup_current_context);
		}

		glfwSwapBuffers(window);
	}

	// Cleanup
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}
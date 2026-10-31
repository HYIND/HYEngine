
#include "stdafx.h"
#include "framework.h"
#include "Editor.h"

#include <iostream>

#include "ImguiLayout.h"

#include <stdio.h>
#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

#include "EditorApp.h"
#include "EditorGlfwInput.h"
#include "Input/EngineInputContext.h"
#include "Input/EngineKey.h"

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

	//ImVec2 mousePos((float)mouseX + winX, (float)mouseY + winY);
	ImVec2 mousePos((float)mouseX, (float)mouseY);

	if (count <= 0)
		return;

	for (int i = 0; i < count; i++)
	{
		std::string path = paths[i];
		if (!fs::path(path).is_absolute())
			continue;

		if (fs::is_directory(fs::path(path)))
			ImguiLayout::DropFolder(path, EditorApp::Instance()->GetProjectManager(), mousePos);
		if (fs::is_regular_file(fs::path(path)))
			ImguiLayout::DropFile(path, EditorApp::Instance()->GetProjectManager(), mousePos);
	}
}

static bool firstFrame = true;
ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

// ---- 输入事件：GLFW 回调直接推给引擎的输入上下文 ----
// 编辑器是 GLFW 宿主，键鼠是真正的窗口事件，不在 UI 帧里轮询 ImGui 的按键状态。
// ImGui 的 GLFW 后端会把自己的回调链在"先注册的回调"后面，所以这些回调必须在
// ImGui_ImplGlfw_InitForVulkan 之前注册，两边的回调才会都收到事件。
static EngineInputContext* g_editorInputContext = nullptr;

static bool ShouldForwardInputToEngine()
{
	if (g_editorInputContext == nullptr)
		return false;

	// 正在文本框里打字时，按键归 UI，不该带着相机/角色跑；
	// 顺手清掉电平，免得打字期间按下的键在退出输入框后还是按下状态。
	if (ImGui::GetIO().WantTextInput)
	{
		g_editorInputContext->ReleaseAllKeys();
		return false;
	}

	return true;
}

static void glfw_key_callback(GLFWwindow* /*window*/, int key, int /*scancode*/, int action, int /*mods*/)
{
	if (!ShouldForwardInputToEngine())
		return;

	// 按住不放的自动重复没有意义：边沿由输入上下文按"抬起 -> 按下"锁存。
	if (action == GLFW_REPEAT)
		return;

	const EngineKey engineKey = EngineKeyFromGlfwKey(key);
	if (engineKey == EngineKey::Unknown)
		return;

	g_editorInputContext->SetKeyDown(engineKey, action != GLFW_RELEASE);
}

static void glfw_mouse_button_callback(GLFWwindow* /*window*/, int button, int action, int /*mods*/)
{
	if (!ShouldForwardInputToEngine())
		return;

	const EngineKey engineKey = EngineKeyFromGlfwMouseButton(button);
	if (engineKey == EngineKey::Unknown)
		return;

	g_editorInputContext->SetKeyDown(engineKey, action != GLFW_RELEASE);
}

// 字符输入：编辑器自己的 ImGui 要收到，引擎那套 UI（RendererUI）也要收到，
// 所以这里接管 GLFW 的回调后两边都转发一次。
static void glfw_char_callback(GLFWwindow* /*window*/, unsigned int codepoint)
{
	ImGui::GetIO().AddInputCharacter(codepoint);

	if (!ShouldForwardInputToEngine() || g_editorInputContext == nullptr)
		return;

	char utf8[5] = {};
	ImTextCharToUtf8(utf8, codepoint);
	g_editorInputContext->PushTextInput(utf8);
}

static void glfw_scroll_callback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset)
{
	ImGui::GetIO().AddMouseWheelEvent(0.0f, static_cast<float>(yoffset));

	if (!ShouldForwardInputToEngine() || g_editorInputContext == nullptr)
		return;

	g_editorInputContext->PushMouseWheel(static_cast<float>(yoffset));
}

static void glfw_window_focus_callback(GLFWwindow* /*window*/, int focused)
{
	if (g_editorInputContext == nullptr)
		return;

	// 窗口失焦就收不到 keyup，必须主动清，否则相机会一直朝一个方向跑。
	if (focused == GLFW_FALSE)
		g_editorInputContext->ReleaseAllKeys();
}

#include "ImguiInit/VulkanInit.h"

int ImGuiMain()
{

	// 1. 初始化 GLFW
	glfwSetErrorCallback(glfw_error_callback);
	if (!glfwInit())
		return 1;

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

	float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
	GLFWmonitor* monitor = glfwGetPrimaryMonitor();
	int workAreaX, workAreaY, workAreaWidth, workAreaHeight;
	glfwGetMonitorWorkarea(monitor, &workAreaX, &workAreaY, &workAreaWidth, &workAreaHeight);
	GLFWwindow* window = glfwCreateWindow((int)(workAreaWidth * 0.8f * main_scale), (int)(workAreaHeight * 0.8f * main_scale), "Editor", nullptr, nullptr);
	if (window == nullptr)
		return 1;

	glfwSetDropCallback(window, glfw_filedrop_callback);

	// 2. 初始化 Vulkan：实例与设备由引擎创建，编辑器只拿句柄去接自己的 GLFW
	//    表面和 ImGui 后端（交换链归 ImGui 后端，引擎不参与）。
	uint32_t extensions_count = 0;
	const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&extensions_count);

	EngineRenderContextDesc renderContextDesc;
	for (uint32_t i = 0; i < extensions_count; i++)
		renderContextDesc.instanceExtensions.push_back(glfw_extensions[i]);

	auto* editorApp = EditorApp::Instance();
	if (!editorApp->CreateRenderContext(renderContextDesc))
		return 1;

	auto vulkanContext = editorApp->GetVulkanContext();
	if (!vulkanContext.valid)
		return 1;

	// 3. 创建窗口表面
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkResult err = glfwCreateWindowSurface((VkInstance)vulkanContext.instance, window, g_Allocator, &surface);
	check_vk_result(err);

	SetupVulkan(
		(VkInstance)vulkanContext.instance,
		(VkPhysicalDevice)vulkanContext.physicalDevice,
		(VkDevice)vulkanContext.device,
		vulkanContext.graphicsQueueFamily,
		(VkQueue)vulkanContext.graphicsQueue,
		(VkDescriptorPool)vulkanContext.descriptorPool
	);

	int w, h;
	glfwGetFramebufferSize(window, &w, &h);
	ImGui_ImplVulkanH_Window* wd = &g_MainWindowData;
	SetupVulkanWindow(wd, surface, w, h);

	// 初始化 ImGui
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
	//io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
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


	g_editorInputContext = editorApp->GetInputContext().get();
	glfwSetKeyCallback(window, glfw_key_callback);
	glfwSetMouseButtonCallback(window, glfw_mouse_button_callback);
	glfwSetWindowFocusCallback(window, glfw_window_focus_callback);

	// 初始化 ImGui 后端
	ImGui_ImplGlfw_InitForVulkan(window, true);

	// 字符回调要在 ImGui 后端之后注册（它会覆盖后端自己的 CharCallback，我们手动转发）。
	glfwSetCharCallback(window, glfw_char_callback);
	glfwSetScrollCallback(window, glfw_scroll_callback);

	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = g_Instance;
	init_info.PhysicalDevice = g_PhysicalDevice;
	init_info.Device = g_Device;
	init_info.QueueFamily = g_QueueFamily;
	init_info.Queue = g_Queue;
	init_info.PipelineCache = g_PipelineCache;
	init_info.DescriptorPool = g_DescriptorPool;
	init_info.MinImageCount = g_MinImageCount;
	init_info.ImageCount = wd->ImageCount;
	init_info.Allocator = g_Allocator;
	init_info.PipelineInfoMain.RenderPass = wd->RenderPass;
	init_info.PipelineInfoMain.Subpass = 0;
	init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	init_info.CheckVkResultFn = check_vk_result;

	{
		LockGuard guard_queue(VKCONTEXT->GetDevice()->GetGraphicsQueueMutex());
		LockGuard guard_pool(VKCONTEXT->GetDescriptorPoolRequestMutex());
		ImGui_ImplVulkan_Init(&init_info);
	}

	ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
	ImGuizmo::Enable(true);

	{
		EditorApp::Instance()->Scene().InitDefaultWorld();
	}

	static bool show = false;

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		// 处理窗口大小变化
		int fb_width, fb_height;
		glfwGetFramebufferSize(window, &fb_width, &fb_height);
		if (fb_width > 0 && fb_height > 0 &&
			(g_SwapChainRebuild || g_MainWindowData.Width != fb_width || g_MainWindowData.Height != fb_height)) {
			ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
			ImGui_ImplVulkanH_CreateOrResizeWindow(
				g_Instance, g_PhysicalDevice, g_Device, wd,
				g_QueueFamily, g_Allocator,
				fb_width, fb_height, g_MinImageCount, 0
			);
			g_MainWindowData.FrameIndex = 0;
			g_SwapChainRebuild = false;
		}

		if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
			ImGui_ImplGlfw_Sleep(10);
			continue;
		}

		// 引擎 UI 的拾取需要窗口坐标下的鼠标位置；视口矩形由 DrawSceneView 每帧写入。
		if (auto inputContext = editorApp->GetInputContext())
		{
			double cursorX = 0.0;
			double cursorY = 0.0;
			glfwGetCursorPos(window, &cursorX, &cursorY);
			inputContext->SetMousePosition(static_cast<float>(cursorX), static_cast<float>(cursorY));
		}

		editorApp->NewInputFrame();

		// 开始 ImGui 帧
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		auto editorApp = EditorApp::Instance();
		auto projectManager = editorApp->GetProjectManager();

		ImguiLayout::DrawMainMenu(window, editorApp, projectManager);
		ImguiLayout::DrawPlayControls(editorApp, projectManager);
		editorApp->Scene().UpdateEditorFrame(projectManager);
		// ============ 创建 Dockspace（直接在主窗口内容区域） ============
		ImguiLayout::SetDockspace(firstFrame);
		if (firstFrame) firstFrame = false;
		// ============ 创建各个窗口（会自动停靠到 Dockspace） ============
		ImguiLayout::DrawSceneObjectList(editorApp);// 场景物体窗口 (左侧)
		ImguiLayout::DrawOptions(editorApp);	// 场景物体窗口 (左侧)
		ImguiLayout::DrawObjectProperties(projectManager);// 属性面板 (右侧)
		ImguiLayout::DrawAssetBrowser(projectManager);	// 资产管理 (底部)
		ImguiLayout::DrawStatusBar(editorApp);// 底部状态栏
		ImguiLayout::DrawSceneView(editorApp, projectManager);// 场景视图 (中间)

		// 渲染
		ImGui::Render();
		ImDrawData* draw_data = ImGui::GetDrawData();

		// 多视口支持（如果启用）
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}

		bool minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);

		{
			LockGuard guard_queue(VKCONTEXT->GetDevice()->GetGraphicsQueueMutex());
			LockGuard guard_pool(VKCONTEXT->GetDescriptorPoolRequestMutex());

			// 渲染主窗口
			if (!minimized) {
				// 设置清除颜色
				wd->ClearValue.color.float32[0] = clear_color.x;
				wd->ClearValue.color.float32[1] = clear_color.y;
				wd->ClearValue.color.float32[2] = clear_color.z;
				wd->ClearValue.color.float32[3] = clear_color.w;
				FrameRender(wd, draw_data);
				FramePresent(wd);
			}
		}

		if (!show)
		{
			glfwShowWindow(window);
			show = true;
		}
	}

	// ==================== 清理 ====================
	err = vkDeviceWaitIdle(g_Device);
	check_vk_result(err);

	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	CleanupVulkanWindow(&g_MainWindowData);
	CleanupVulkan();

	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}


int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	SetThreadDescription(GetCurrentThread(), L"EditorMainThread");

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

	return ImGuiMain();
}

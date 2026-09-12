#include "vkstdafx.h"
#include "stdafx.h"
#include "framework.h"
#include "Editor.h"

#include <iostream>


#include <stdio.h>
#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

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
	}
}

static bool firstFrame = true;
ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);


#include "VulkanRenderEngine\VKCore\CoreGeneral.h"
#include "VulkanRenderEngine\VKWrapper\WrapperGeneral.h"
#include "VulkanRenderEngine\VKContext.h"
#include "VulkanRenderEngine\Base\Model.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"
#include "VulkanRenderEngine/VulkanRenderer.h"
#include "CommonData/Camera.h"
#include <ranges>

using namespace VKCore;
using namespace VKWrapper;

std::shared_ptr<VulkanRenderer> vkRenderer;
glm::uvec2 windowSize = { 1280,720 };


GLFWwindow* pWindow;
GLFWmonitor* pMonitor;
const char* windowTitle = "EasyVK";
bool InitializeWindow(VkExtent2D size, bool fullScreen = false, bool isResizable = true, bool limitFrameRate = false)
{
	if (!glfwInit()) {
		std::cout << std::format("[ InitializeWindow ] ERROR\nFailed to initialize GLFW!\n");
		return false;
	}
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, isResizable);
	pMonitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* pMode = glfwGetVideoMode(pMonitor);
	pWindow = fullScreen ?
		glfwCreateWindow(pMode->width, pMode->height, windowTitle, pMonitor, nullptr) :
		glfwCreateWindow(size.width, size.height, windowTitle, nullptr, nullptr);
	if (!pWindow) {
		std::cout << std::format("[ InitializeWindow ]\nFailed to create a glfw window!\n");
		glfwTerminate();
		return false;
	}

	uint32_t extensionCount = 0;
	const char** extensionNames;
	extensionNames = glfwGetRequiredInstanceExtensions(&extensionCount);
	if (!extensionNames) {
		std::cout << std::format("[ InitializeWindow ]\nVulkan is not available on this machine!\n");
		glfwTerminate();
		return false;
	}

	auto vulkanInstance = VulkanRenderer::CreateInstance(extensionCount, extensionNames);

	//创建window surface
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	if (VkResult result = glfwCreateWindowSurface(vulkanInstance->GetHandle(), pWindow, nullptr, &surface)) {
		std::cout << std::format("[ InitializeWindow ] ERROR\nFailed to create a window surface!\nError code: {}\n", string_VkResult(result));
		glfwTerminate();
		return false;
	}

	vkRenderer = VulkanRenderer::CreateForWindow(vulkanInstance, surface, windowSize.x, windowSize.y);

	return vkRenderer != nullptr;
}

void TerminateWindow() {
	if (vkRenderer->GetVulkanDevice())
		vkRenderer->GetVulkanDevice()->WaitIdle();
	glfwTerminate();
}

void TitleFps() {
	static double time0 = glfwGetTime();
	static double time1;
	static double dt;
	static int dframe = -1;
	static std::stringstream info;
	time1 = glfwGetTime();
	dframe++;
	if ((dt = time1 - time0) >= 1) {
		info.precision(1);
		info << windowTitle << "    " << std::fixed << dframe / dt << " FPS";
		glfwSetWindowTitle(pWindow, info.str().c_str());
		info.str(""); //别忘了在设置完窗口标题后清空所用的stringstream
		time0 = time1;
		dframe = 0;
	}
}

struct OrbitParams {
	float angle = 0.0f;
	float radius = 5.0f;
	glm::vec3 center = glm::vec3(0, 3, 0);
};

// 独立的更新函数
void UpdateOrbitCamera(Camera& camera, float deltaTime)
{
	OrbitParams params;

	// 旋转速度：每秒30度
	float rotationSpeed = 30.0f; // 度/秒
	params.angle += rotationSpeed * deltaTime;
	if (params.angle > 360.0f) params.angle -= 360.0f;

	// 计算新的相机位置
	float rad = glm::radians(params.angle);
	float x = params.radius * sin(rad);
	float z = params.radius * cos(rad);

	glm::vec3 newPosition = params.center + glm::vec3(x, 0, z);

	// 更新相机位置和朝向
	camera.SetPosition(newPosition);
	camera.SetDirection(params.center - newPosition);
}

int VulkanMain()
{
	if (!InitializeWindow({ 1280, 720 }))
		return -1;

	auto keqing_model = std::make_shared<Model>("Test/keqing2/keqing.pmx");
	keqing_model->MakeScale(glm::vec3(0.25f));

	Camera camera({}, {}, { 0,1,0 });
	camera.SetPosition(glm::vec3(0, 3, 5));
	camera.SetDirection(glm::vec3(0, 0, -1));
	camera.SetFarPlane(150);

	while (!glfwWindowShouldClose(pWindow))
	{
		while (glfwGetWindowAttrib(pWindow, GLFW_ICONIFIED))
			glfwWaitEvents();

		UpdateOrbitCamera(camera, glfwGetTime());

		RenderState state;
		state.camera.projection = camera.GetPerspectiveProjectionMatrix(windowSize.x, windowSize.y);
		state.camera.view = camera.GetViewMatrix();
		state.camera.position = camera.GetPosition();
		state.camera.direction = camera.GetDirection();
		state.camera.directionUp = camera.GetDirectionUp();
		state.camera.directionRight = camera.GetDirectionRight();
		state.camera.nearPlane = camera.GetNearPlane();
		state.camera.farPlane = camera.GetFarPlane();
		state.camera.fov = camera.GetFOV();
		state.camera.frustum = Frustum(state.camera.projection * state.camera.view);

		for (auto& info : keqing_model->getMeshInfos())
		{
			VKRenderObjectData::SceneRenderData::OpaqueMeshItem item;
			item.meshinfo = info;
			item.transform = item.prevTransform = glm::mat4(1.f);
			state.objects.sceneRenderData.opaqueMesh.push_back(item);
		}


		auto dirInfo = std::make_shared<DirLightInfo>();
		dirInfo->light = std::make_shared<DirLight>(glm::vec3(-1, 0, -1));
		dirInfo->light->setCascadeLevel(4);

		auto pointInfo = std::make_shared<PointLightInfo>();
		pointInfo->light = std::make_shared<PointLight>(glm::vec3(2, 2, 6));
		pointInfo->light->setIntensity(50);

		state.lights.dirLightInfos.push_back(dirInfo);
		state.lights.pointLightInfos.push_back(pointInfo);

		VKCONTEXT->ProcessPendingCommandAndWait();
		vkRenderer->EarlyProcess(state);
		vkRenderer->Draw(state);

		glfwPollEvents();
		TitleFps();
	}

	TerminateWindow();
	return 0;
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

	return VulkanMain();
}
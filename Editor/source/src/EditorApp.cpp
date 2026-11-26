#include "EditorApp.h"
#include "CommonSystems.h"
#include "PlatformRuntimeSystems.h"
#include "ECSCore/World.h"
#include "Helper/DynamicFpsController.h"
#include "Project/ProjectManager.h"

#include "CommonData/LightFactory.h"
#include "VulkanRenderEngine/General/RenderHelper.h"
#include "CommonComponent.h"
#include "PlatformRuntimeComponents.h"

#include "Input/EngineInputContext.h"
#include "RenderEngine/Renderer3D.h"

#include "EditorTags.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <iostream>
#include <stdexcept>
#include <Windows.h>


static std::filesystem::path GetEditorExecutableDirectory()
{
	std::vector<wchar_t> pathBuffer(32768);
	const DWORD length = GetModuleFileNameW(nullptr, pathBuffer.data(), static_cast<DWORD>(pathBuffer.size()));
	if (length == 0 || length >= pathBuffer.size())
		return std::filesystem::current_path();
	return std::filesystem::path(std::wstring(pathBuffer.data(), length)).parent_path();
}

EditorApp* EditorApp::Instance() {
	static EditorApp* instance = new EditorApp();
	return instance;
}

EditorApp::EditorApp()
	: m_scene(m_runtime)
{
	// 中键平移按视口高度把像素换算成世界单位，视口高度只有应用层知道。
	m_scene.SetViewportHeightProvider([this] { return static_cast<float>(GetRenderTargetHeight()); });

	EngineRuntimeConfig config;
	config.hostType = EngineHostType::Editor;
	if (!m_runtime.Initialize(config))
		throw std::runtime_error("Failed to initialize EngineRuntime");
	std::string moduleError;
	if (!m_runtime.LoadModules((GetEditorExecutableDirectory() / "Modules").string(), &moduleError))
		throw std::runtime_error("Failed to load project modules: " + moduleError);
}

EditorApp::~EditorApp() {}

void EditorApp::Shutdown()
{
	// 默认场景共用的天空盒：放掉缓存，场景销毁时它就跟着释放
	SceneEditor::ReleaseSharedResources();
	m_runtime.Shutdown();
}

void EditorApp::DestroyRenderContext()
{
	m_runtime.DestroyRenderContext();
}

bool EditorApp::CreateRenderContext(const EngineRenderContextDesc& desc)
{
	std::string error;
	if (!m_runtime.CreateRenderContext(desc, &error))
	{
		std::cout << std::format("[ EditorApp ] 创建渲染上下文失败：{}\n", error);
		return false;
	}
	return true;
}

EngineVulkanContext EditorApp::GetVulkanContext() const
{
	return m_runtime.GetVulkanContext();
}

bool EditorApp::CreateRenderTarget(uint32_t width, uint32_t height)
{
	// 走 Renderer 门面：目标（最终画面 + 各阶段的中间目标）都由它的帧流水线管理。
	auto* renderer = m_runtime.GetRenderer();
	return renderer && renderer->CreateRenderTarget(width, height);
}

bool EditorApp::ResizeRenderTarget(uint32_t width, uint32_t height, const std::function<void()>& beforeResize)
{
	auto* renderer = m_runtime.GetRenderer();
	return renderer && renderer->ResizeRenderTarget(width, height, beforeResize);
}

bool EditorApp::IsRenderDeviceReady() const
{
	auto* renderer = m_runtime.GetRenderer();
	return renderer && renderer->IsRenderDeviceReady();
}

bool EditorApp::IsRenderTargetReady() const
{
	auto* renderer = m_runtime.GetRenderer();
	return renderer && renderer->IsRenderTargetReady();
}

uint32_t EditorApp::GetRenderTargetWidth() const
{
	auto* renderer = m_runtime.GetRenderer();
	return renderer ? renderer->GetRenderTargetWidth() : 0u;
}

uint32_t EditorApp::GetRenderTargetHeight() const
{
	auto* renderer = m_runtime.GetRenderer();
	return renderer ? renderer->GetRenderTargetHeight() : 0u;
}

void EditorApp::StartSceneRender()
{
	if (auto* renderer = m_runtime.GetRenderer())
		renderer->Start();
}

void EditorApp::StopSceneRender()
{
	if (auto* renderer = m_runtime.GetRenderer())
		renderer->StopFrameLoop();
}

void EditorApp::WaitImage(const std::function<void(std::shared_ptr<Texture2D>)>& callback) {
	if (auto* renderer = m_runtime.GetRenderer())
		renderer->WaitImage([&](std::shared_ptr<Texture2D> tex, std::shared_ptr<RenderState>) {
		callback(tex);
			});
}

Scene3DRenderOption EditorApp::GetOption() const
{
	auto* renderer3D = m_runtime.GetRenderer3D();
	return renderer3D ? renderer3D->GetOption() : Scene3DRenderOption{};
}

void EditorApp::SetOption(Scene3DRenderOption option)
{
	if (auto* renderer3D = m_runtime.GetRenderer3D())
		renderer3D->SetOption(option);
}

Render::Renderer::LayerVisibility EditorApp::GetLayerVisibility() const
{
	auto* renderer = m_runtime.GetRenderer();
	return renderer ? renderer->GetLayerVisibility() : Render::Renderer::LayerVisibility{};
}

void EditorApp::SetLayerVisibility(const Render::Renderer::LayerVisibility& visibility)
{
	if (auto* renderer = m_runtime.GetRenderer())
		renderer->SetLayerVisibility(visibility);
}

void EditorApp::SetTargetFps(uint32_t fps)
{
	m_runtime.SetTargetFps(fps);
}

uint32_t EditorApp::GetTargetFps() const
{
	return m_runtime.GetTargetFps();
}

bool EditorApp::ResizeRenderer(uint32_t width, uint32_t height, std::function<void()> prevClear)
{
	if (width == GetRenderTargetWidth() && height == GetRenderTargetHeight())
	{
		resizePending = false;
		return false;
	}

	if (pendingWidth != width || pendingHeight != height)
	{
		pendingWidth = width;
		pendingHeight = height;
		resizePending = true;
		resizeTimeStamp = Tool::GetTimestampMilliseconds();
		return false;
	}

	// 新尺寸先按住一小会儿，免得拖动视口分割条时每帧都重建 target。
	if (!resizePending || Tool::GetTimestampMilliseconds() - resizeTimeStamp <= 500)
		return false;

	resizePending = false;
	return ResizeRenderTarget(pendingWidth, pendingHeight, prevClear);
}

World* EditorApp::GetWorld()
{
	return m_scene.GetWorld();
}

bool EditorApp::GetCameraSnapshot(glm::mat4& view, glm::mat4& projection, glm::vec3& position, glm::vec3& direction) const
{
	auto* renderer3D = m_runtime.GetRenderer3D();
	if (!renderer3D)
		return false;

	Render::Renderer3D::CameraSnapshot snapshot;
	if (!renderer3D->GetCameraSnapshot(snapshot))
		return false;

	view = snapshot.view;
	projection = snapshot.projection;
	position = snapshot.position;
	direction = snapshot.direction;
	return true;
}
ProjectManager* EditorApp::GetProjectManager() const
{
	return m_runtime.GetProjectManager();
}

std::shared_ptr<EngineInputContext> EditorApp::GetInputContext() const
{
	return m_runtime.GetInputContext();
}

void EditorApp::NewInputFrame()
{
	if (auto inputContext = m_runtime.GetInputContext())
		inputContext->NewFrame();
}


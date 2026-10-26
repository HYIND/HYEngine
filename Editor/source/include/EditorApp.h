#pragma once

#include "ECSCore/World.h"
#include "VulkanRenderEngine/VulkanRenderer.h"
#include "RenderEngine/RenderFrameManager.h"
#include "CommonSystems.h"
#include "CommonData/LightData.h"
#include "EngineRuntime.h"
#include "SceneEditor.h"

#include <functional>
#include <memory>

class ProjectManager;
class EngineInputContext;

// 编辑器应用：宿主进程里唯一的实例。持有引擎运行时与离屏渲染 target，
// 管理帧消费者与渲染选项；场景内容与编辑/播放状态在 SceneEditor 里。
class EditorApp
{
public:
	static EditorApp* Instance();

private:
	EditorApp();
	~EditorApp();

public:
	bool CreateRenderContext(const EngineRenderContextDesc& desc);
	EngineVulkanContext GetVulkanContext() const;

	// 离屏 target 归 Renderer3D 所有。编辑器只报告视口尺寸、向服务询问 target 状态，
	// 不直接碰 Vulkan 后端。
	bool CreateRenderTarget(uint32_t width, uint32_t height);
	bool ResizeRenderTarget(uint32_t width, uint32_t height, const std::function<void()>& beforeResize = {});
	bool IsRenderDeviceReady() const;
	bool IsRenderTargetReady() const;
	uint32_t GetRenderTargetWidth() const;
	uint32_t GetRenderTargetHeight() const;

	// 渲染线程最近一帧的相机矩阵：Gizmo 与拾取都用它，不要去抢生产者的三缓冲。
	bool GetCameraSnapshot(glm::mat4& view, glm::mat4& projection, glm::vec3& position, glm::vec3& direction) const;

	// 工程对象归引擎所有，编辑器从这里拿指针，不再自己持有 ProjectManager。
	ProjectManager* GetProjectManager() const;

	// 宿主把窗口事件（GLFW 回调）直接推给引擎的输入上下文；引擎不轮询窗口状态。
	std::shared_ptr<EngineInputContext> GetInputContext() const;

	// 帧边界：宿主每帧调一次，把这一轮收到的按键事件拍成这一帧的输入帧。
	// 必须在 UI 与场景推进之前调用，和 ImGui::NewFrame 是同一层的事情。
	void NewInputFrame();

	// 只负责启停 3D 帧消费者，Vulkan 后端保持运行；拆后端、重建 target 是服务的职责。
	void StartSceneRender();
	void StopSceneRender();

	void WaitImage(const std::function<void(std::shared_ptr<Texture2D>)>& callback);

	Scene3DRenderOption GetOption() const;
	void SetOption(Scene3DRenderOption option);

	void SetTargetFps(uint32_t fps);
	uint32_t GetTargetFps() const;

	bool ResizeVulkan(uint32_t width, uint32_t height, std::function<void()> prevClear);

	// 场景会话：World、编辑/播放状态、拾取、场景内容搭建。
	SceneEditor& Scene() { return m_scene; }
	World* GetWorld();

private:
	EngineRuntime m_runtime;
	SceneEditor m_scene;

	uint32_t pendingWidth = 0, pendingHeight = 0;
	int64_t resizeTimeStamp = 0;
	bool resizePending = false;
};

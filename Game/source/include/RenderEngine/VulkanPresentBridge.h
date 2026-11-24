#pragma once

#include "VulkanRenderEngine/SharedTexture.h"

#include <memory>
#include <string>

namespace Render { class Renderer; }

// Game 专用的输出桥：引擎在 Vulkan 里离屏渲染（3D -> 2D 层 -> UI 都已经合成好），
// 这里把那张图搬进 D3D11 交换链的后台缓冲并 Present。
// D3D11 设备、立即上下文和交换链是 Game 共用的图形上下文（Manager/GraphicsContextManager.h），
// 在界面线程 WM_CREATE 里建好；桥只是使用方，不负责创建和释放。
class VulkanPresentBridge
{
public:
	VulkanPresentBridge() = default;
	~VulkanPresentBridge();

	VulkanPresentBridge(const VulkanPresentBridge&) = delete;
	VulkanPresentBridge& operator=(const VulkanPresentBridge&) = delete;

	// 检查共用上下文可用，创建与 Vulkan 共享的纹理（渲染线程里调用）
	bool Initialize(Render::Renderer& renderer, std::string* error = nullptr);
	void Shutdown();

	// 等引擎产出一帧 → blit 到共享纹理 → AcquireSync → CopyResource 到后台缓冲 → Present
	void PresentFrame() const;

	bool IsReady() const { return _ready; }
	const std::shared_ptr<SharedTexture>& GetSharedTexture() const { return _sharedTexture; }

private:
	Render::Renderer* _renderer = nullptr;
	std::shared_ptr<SharedTexture> _sharedTexture;
	bool _allowTearing = false;	// 取自共用交换链建链时的标志

	bool _ready = false;
};

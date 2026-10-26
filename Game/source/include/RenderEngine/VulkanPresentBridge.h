#pragma once

#include "VulkanRenderEngine/SharedTexture.h"

#include <memory>

namespace Render { class Renderer; }

// Game 专用的输出桥：把引擎离屏渲染出来的 Vulkan 图像搬进 D3D11 后台缓冲。
class VulkanPresentBridge
{
public:
	VulkanPresentBridge() = default;
	~VulkanPresentBridge() = default;

	VulkanPresentBridge(const VulkanPresentBridge&) = delete;
	VulkanPresentBridge& operator=(const VulkanPresentBridge&) = delete;

	bool Initialize(Render::Renderer& renderer);

	// 等引擎产出一帧 → blit 到共享纹理 → AcquireSync → CopyResource 到后台缓冲。
	void PresentFrame() const;

	bool IsReady() const { return _ready; }
	const std::shared_ptr<SharedTexture>& GetSharedTexture() const { return _sharedTexture; }

private:
	Render::Renderer* _renderer = nullptr;
	std::shared_ptr<SharedTexture> _sharedTexture;
	bool _ready = false;
};

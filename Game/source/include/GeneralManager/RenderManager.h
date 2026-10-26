#pragma once

#include "RenderEngine/D2DTools.h"
#include "RenderEngine/Renderer.h"
#include "Helper/DynamicFpsController.h"
#include "RenderEngine/RenderFrameData.h"
#include "RenderEngine/VulkanPresentBridge.h"
#include "Helper/TripleBuffer.h"
#include <memory>
#include "VulkanRenderEngine/RenderOption.h"

class RenderManager
{
public:
	static RenderManager* Instance();
	~RenderManager();

	// 3D 输出桥（引擎离屏 → 共享纹理 → D3D11 后台缓冲），归宿主所有。
	std::shared_ptr<VulkanPresentBridge> getVulkanBridge();

	Scene3DRenderOption GetOption() const;
	void SetOption(Scene3DRenderOption option);

private:
	RenderManager();

private:
	std::shared_ptr<VulkanPresentBridge> _vulkanBridge;
};



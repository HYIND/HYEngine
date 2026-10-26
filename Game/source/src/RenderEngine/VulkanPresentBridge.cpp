#include "RenderEngine/VulkanPresentBridge.h"

#include "RenderEngine/D2DTools.h"
#include "RenderEngine/Renderer.h"
#include "VulkanRenderEngine/Base/Texture2D.h"
#include "VulkanRenderEngine/VKContext.h"


bool VulkanPresentBridge::Initialize(Render::Renderer& renderer)
{
	if (_ready)
		return true;

	if (!renderer.HasRenderer())
		return false;

	if (!_sharedTexture)
		_sharedTexture = CreateSharedTexture(g_pD3DDevice, renderer.GetRenderTargetWidth(), renderer.GetRenderTargetHeight(), DXGI_FORMAT_B8G8R8A8_UNORM);
	if (!_sharedTexture)
		return false;

	_renderer = &renderer;
	_ready = true;
	return true;
}

void VulkanPresentBridge::PresentFrame() const
{
	if (!_ready || !_sharedTexture || !g_pSwapChain || !_renderer)
		return;

	_renderer->WaitImage([&](std::shared_ptr<Texture2D> tex, std::shared_ptr<RenderState> state) {
		if (!tex || !_sharedTexture->vulkanTexture)
			return;

		auto cmd = VKCONTEXT->GetCommandBuffer();
		Texture2D::BlitImageAsync(cmd, tex, _sharedTexture->vulkanTexture, vk::Filter::eNearest);
		if (cmd->IsRecording())
			VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
		});

	ID3D11Texture2D* pBackBuffer = nullptr;
	auto hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	if (FAILED(hr))
		return;

	IDXGIKeyedMutex* pMutex = nullptr;
	hr = _sharedTexture->d3dTexture->QueryInterface(IID_PPV_ARGS(&pMutex));
	if (SUCCEEDED(hr) && pMutex)
	{
		hr = pMutex->AcquireSync(0, 0);
		if (FAILED(hr))
		{
			// 超时或错误，这一帧不能读
			pMutex->Release();
			pBackBuffer->Release();
			return;
		}
	}

	g_pD3DContext->CopyResource(pBackBuffer, _sharedTexture->d3dTexture);
	pBackBuffer->Release();

	if (pMutex)
	{
		pMutex->ReleaseSync(0);
		pMutex->Release();
	}
}

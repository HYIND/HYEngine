#include "RenderEngine/VulkanPresentBridge.h"

#include "Manager/GraphicsContextManager.h"
#include "RenderEngine/Renderer.h"
#include "VulkanRenderEngine/Base/Texture2D.h"
#include "VulkanRenderEngine/VKContext.h"

#include <dxgi1_5.h>

#pragma comment(lib, "dxgi.lib")

VulkanPresentBridge::~VulkanPresentBridge()
{
	Shutdown();
}

bool VulkanPresentBridge::Initialize(Render::Renderer& renderer, std::string* error)
{
	if (_ready)
		return true;

	auto fail = [&](const char* message) {
		if (error) *error = message;
		return false;
		};

	if (!renderer.HasRenderer())
		return fail("渲染器还没创建");

	// 共用图形上下文必须在界面线程 WM_CREATE 里先建好（GraphicsContextManager::InitSharedGraphicsContext）
	if (!g_pD3DDevice || !g_pD3DContext || !g_pSwapChain)
		return fail("共用图形上下文还没创建（应在启动渲染线程前调用 GraphicsContextManager::InitSharedGraphicsContext()）");

	// 交换链是否允许 tearing：按建链时带没带 ALLOW_TEARING 标志决定
	DXGI_SWAP_CHAIN_DESC desc{};
	if (SUCCEEDED(g_pSwapChain->GetDesc(&desc)))
		_allowTearing = (desc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0;

	if (!_sharedTexture)
		_sharedTexture = CreateSharedTexture(g_pD3DDevice, renderer.GetRenderTargetWidth(), renderer.GetRenderTargetHeight(), DXGI_FORMAT_B8G8R8A8_UNORM);
	if (!_sharedTexture)
		return fail("创建 D3D11/Vulkan 共享纹理失败");

	_renderer = &renderer;
	_ready = true;
	return true;
}

void VulkanPresentBridge::Shutdown()
{
	_ready = false;
	_renderer = nullptr;
	if (_sharedTexture)
	{
		ReleaseSharedTexture(_sharedTexture.get());
		_sharedTexture.reset();
	}
	// 设备、立即上下文和交换链属于共用图形上下文（GraphicsContextManager），桥不释放
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

	ID3D11Texture2D* backBuffer = nullptr;
	if (FAILED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
		return;

	IDXGIKeyedMutex* mutex = nullptr;
	if (SUCCEEDED(_sharedTexture->d3dTexture->QueryInterface(IID_PPV_ARGS(&mutex))) && mutex)
	{
		if (FAILED(mutex->AcquireSync(0, 0)))
		{
			// 超时或错误：这一帧不能读
			mutex->Release();
			backBuffer->Release();
			return;
		}
	}

	g_pD3DContext->CopyResource(backBuffer, _sharedTexture->d3dTexture);
	backBuffer->Release();

	if (mutex)
	{
		mutex->ReleaseSync(0);
		mutex->Release();
	}

	g_pSwapChain->Present(0, _allowTearing ? DXGI_PRESENT_ALLOW_TEARING : 0);
}

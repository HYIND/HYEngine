#include "vkstdafx.h"
#include "VulkanRenderEngine/SharedTexture.h"
#include "VulkanRenderEngine/Base/Texture2D.h"

std::shared_ptr<SharedTexture> CreateSharedTexture(
	ID3D11Device* pD3DDevice,
	uint32_t width,
	uint32_t height,
	DXGI_FORMAT format
) {
	if (pD3DDevice == nullptr) {
		return nullptr;
	}

	if (width <= 0 || height <= 0) {
		return nullptr;
	}

	HRESULT hr = S_OK;

	// ============================================================
	// 1. 填充纹理描述
	// ============================================================
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;  // GPU 只读/只写，CPU 不访问
	desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

	// ============================================================
	// 2. 创建纹理
	// ============================================================
	ID3D11Texture2D* pD3DTexture = nullptr;
	hr = pD3DDevice->CreateTexture2D(&desc, nullptr, &pD3DTexture);
	if (FAILED(hr)) {
		return nullptr;
	}

	// ============================================================
	// 3. 获取共享句柄
	// ============================================================
	// 获取 IDXGIResource1 接口来创建 NT 句柄
	HANDLE sharedHandle = nullptr;

	if (desc.MiscFlags == D3D11_RESOURCE_MISC_SHARED)
	{
		IDXGIResource* pDXGIResource = nullptr;
		hr = pD3DTexture->QueryInterface(IID_PPV_ARGS(&pDXGIResource));
		if (FAILED(hr)) {
			pD3DTexture->Release();
			return nullptr;
		}

		hr = pDXGIResource->GetSharedHandle(&sharedHandle);
		pDXGIResource->Release();
		if (FAILED(hr) || sharedHandle == nullptr) {
			pD3DTexture->Release();
			return nullptr;
		}
	}
	else
	{
		IDXGIResource1* pDXGIResource1 = nullptr;
		hr = pD3DTexture->QueryInterface(IID_PPV_ARGS(&pDXGIResource1));
		if (FAILED(hr)) {
			pD3DTexture->Release();
			return nullptr;
		}

		// 创建 NT 句柄，参数可设置访问权限和名称
		hr = pDXGIResource1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &sharedHandle);
		pDXGIResource1->Release();
		if (FAILED(hr) || sharedHandle == nullptr) {
			pD3DTexture->Release();
			return nullptr;
		}
	}


	auto sharedTex = std::make_shared<SharedTexture>();
	sharedTex->sharedHandle = sharedHandle;
	sharedTex->d3dTexture = pD3DTexture;
	sharedTex->width = width;
	sharedTex->height = height;
	sharedTex->format = format;
	sharedTex->vulkanTexture = std::make_shared<Texture2D>(sharedTex);

	return sharedTex;
}

void ReleaseSharedTexture(SharedTexture* pTexture) {
	if (pTexture == nullptr) {
		return;
	}

	// 释放 D3D 纹理
	if (pTexture->d3dTexture) {
		pTexture->d3dTexture->Release();
		pTexture->d3dTexture = nullptr;
	}

	pTexture->width = 0;
	pTexture->height = 0;
}
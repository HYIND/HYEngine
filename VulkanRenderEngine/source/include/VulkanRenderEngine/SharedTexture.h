#pragma once

#include "vkstdafx.h"
#include <d3d11.h>
#include <d3d11_1.h>


struct SharedTexture {
	HANDLE sharedHandle;
	ID3D11Texture2D* d3dTexture = nullptr;   // D3D11 纹理
	uint32_t width = 0;
	uint32_t height = 0;
	DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM;
};

// 创建共享纹理
std::shared_ptr<SharedTexture> CreateSharedTexture(
	ID3D11Device* pD3DDevice,
	uint32_t width,
	uint32_t height,
	DXGI_FORMAT format
);

// 释放共享纹理
void ReleaseSharedTexture(SharedTexture* pTexture);

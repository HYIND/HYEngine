#pragma once

#include <GL\glew.h>
#include <GL/wglew.h>

#include <d3d11.h>
#include <d3d11_1.h>


struct SharedTexture {
	HANDLE InteropDevice = NULL;
	HANDLE InteropObject = NULL;
	ID3D11Texture2D* d3dTexture = nullptr;   // D3D11 纹理
	GLuint glTex = 0;
	int width = 0;
	int height = 0;
	DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM;//DXGI_FORMAT_R16G16B16A16_FLOAT
};

// 创建共享纹理
bool CreateSharedTexture(
	ID3D11Device* pD3DDevice,
	int width,
	int height,
	DXGI_FORMAT format,
	SharedTexture* pOut
);

// 释放共享纹理
void ReleaseSharedTexture(SharedTexture* pTexture);

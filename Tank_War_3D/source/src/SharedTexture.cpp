#include "RenderEngine/SharedTexture.h"
#include <iostream>

bool CreateSharedTexture(
	ID3D11Device* pD3DDevice,
	int width,
	int height,
	DXGI_FORMAT format,
	SharedTexture* pOut
) {
	if (pD3DDevice == nullptr || pOut == nullptr) {
		return false;
	}

	if (width <= 0 || height <= 0) {
		return false;
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
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;// 这样 OpenGL 可以渲染到它，D3D 也可以读取它做合成
	desc.CPUAccessFlags = 0;  // GPU 只读/只写，CPU 不访问
	desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

	// ============================================================
	// 2. 创建纹理
	// ============================================================
	ID3D11Texture2D* pD3DTexture = nullptr;
	hr = pD3DDevice->CreateTexture2D(&desc, nullptr, &pD3DTexture);
	if (FAILED(hr)) {
		return false;
	}

	// ============================================================
	// 3. 获取共享句柄
	// ============================================================
	IDXGIResource* pDXGIResource = nullptr;
	hr = pD3DTexture->QueryInterface(IID_PPV_ARGS(&pDXGIResource));
	if (FAILED(hr)) {
		pD3DTexture->Release();
		return false;
	}

	HANDLE sharedHandle = nullptr;
	hr = pDXGIResource->GetSharedHandle(&sharedHandle);
	pDXGIResource->Release();

	if (FAILED(hr) || sharedHandle == nullptr) {
		pD3DTexture->Release();
		return false;
	}

	HANDLE hInteropDevice = wglDXOpenDeviceNV(pD3DDevice);
	if (hInteropDevice == NULL) {
		std::cerr << "wglDXOpenDeviceNV error " << GetLastError() << '\n';
		pD3DTexture->Release();
		return false;
	}

	GLuint glTex = 0;
	glGenTextures(1, &glTex);
	if (glTex == 0)
	{
		std::cerr << "glGenTextures error " << '\n';
		pD3DTexture->Release();
		return false;
	}

	// 3. 注册 D3D 纹理为 OpenGL 纹理
	HANDLE hInteropObject = wglDXRegisterObjectNV(
		hInteropDevice,				// 互操作设备句柄
		pD3DTexture,				// D3D 纹理指针
		glTex,						// GL 纹理名称
		GL_TEXTURE_2D,				// 类型：纹理
		WGL_ACCESS_READ_WRITE_NV	// 访问权限
	);
	if (hInteropObject == NULL) {
		std::cerr << "wglDXRegisterObjectNV error " << GetLastError() << '\n';
		pD3DTexture->Release();
		return false;
	}

	pOut->InteropDevice = hInteropDevice;
	pOut->InteropObject = hInteropObject;
	pOut->d3dTexture = pD3DTexture;
	pOut->glTex = glTex;
	pOut->width = width;
	pOut->height = height;
	pOut->format = format;

	return true;
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

	// 注意：sharedHandle 是 Windows 内核句柄，需要 CloseHandle
	// 但 GetSharedHandle 返回的句柄是 D3D 内部管理的，
	// 由 D3D 资源生命周期管理，不需要手动 CloseHandle
	// 如果使用 CreateSharedHandle (NT句柄)，则需要 CloseHandle

	if (pTexture->InteropDevice && pTexture->InteropObject)
		wglDXUnregisterObjectNV(pTexture->InteropDevice, pTexture->InteropObject);

	if (pTexture->InteropDevice)
	{
		wglDXCloseDeviceNV(pTexture->InteropDevice);
		pTexture->InteropDevice = NULL;
	}

	if (pTexture->glTex != 0)
	{
		glDeleteTextures(1, &(pTexture->glTex));
		pTexture->glTex = 0;
	}

	pTexture->width = 0;
	pTexture->height = 0;
}
#pragma once


#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11_4.h>	// ID3D11Multithread
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_5.h>

// d2d 及位图 头文件
#include <d2d1_3.h>
#include <wincodec.h>
#include <dwrite.h>
#include <d2d1_3helper.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "Dwrite.lib")

// 共用的 D3D11 / DXGI 对象
extern ID3D11Device* g_pD3DDevice;
extern ID3D11DeviceContext* g_pD3DContext;
extern IDXGISwapChain1* g_pSwapChain;
extern bool g_bAllowTearing;	// 建交换链时是否带 ALLOW_TEARING 标志（Present 时用）

// 共用的 D2D / DWrite / WIC 对象
extern ID2D1Factory1* pD2DFactory;
extern ID2D1Device* pD2DDevice;
extern ID2D1DeviceContext* pD2DContext;
extern IDWriteFactory* pIDWriteFactory;
extern IWICImagingFactory* pIWICFactory;

class GraphicsContextManager
{
public:
	static GraphicsContextManager* Instance();

	// 图形上下文：D3D11 设备（带 BGRA 支持）+ 交换链 + D2D/DWrite/WIC。
	bool InitSharedGraphicsContext();

private:
	GraphicsContextManager();
};

#ifndef SafeRelease
#define SafeRelease(P) if(P){P->Release() ; P = NULL ;}
#endif

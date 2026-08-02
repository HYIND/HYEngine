#pragma once

#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <dxgi1_2.h>

#pragma comment(lib, "d3d11.lib")

// d2d及位图 头文件
//#include <d2d1.h>
#include <d2d1_3.h>
#include <wincodec.h>
#include <dwrite.h>
#include <d2d1_3helper.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib,"Dwrite.lib")


//extern ID2D1Factory* pD2DFactory;
//extern ID2D1HwndRenderTarget* pRenderTarget;
//extern IWICImagingFactory* pIWICFactory;
//extern IDWriteFactory* pIDWriteFactory;

extern ID2D1Factory1* pD2DFactory;
extern ID2D1DeviceContext* pRenderTarget;
extern IWICImagingFactory* pIWICFactory;
extern IDWriteFactory* pIDWriteFactory;

extern IDXGISwapChain1* g_pSwapChain;
extern ID3D11Device* g_pD3DDevice;
extern ID3D11DeviceContext* g_pD3DContext;

ID2D1Bitmap* LoadResourceBitmap(HINSTANCE hinstance,
	ID2D1RenderTarget* pRenderTarget,
	LPCWSTR resourceType, LPCWSTR resourceName);

ID2D1Bitmap* Loadbitmap(ID2D1RenderTarget* pRenderTarget,
	LPCTSTR pszResource);

bool LoadResourceGIF(
	HINSTANCE hinstance,
	ID2D1RenderTarget* pRenderTarget,
	LPCWSTR resourceType, LPCWSTR resourceName,
	UINT& out_totalFrameCount, IWICBitmapDecoder*& out_pDecoder, IWICStream*& out_pStream);

#define SafeRelease(P) if(P){P->Release() ; P = NULL ;}

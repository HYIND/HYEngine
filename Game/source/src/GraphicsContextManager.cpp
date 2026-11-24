#include "Manager/GraphicsContextManager.h"
#include "GeneralManager/WindowHandleManager.h"

ID3D11Device* g_pD3DDevice = nullptr;
ID3D11DeviceContext* g_pD3DContext = nullptr;
IDXGISwapChain1* g_pSwapChain = nullptr;
bool g_bAllowTearing = false;

ID2D1Factory1* pD2DFactory = nullptr;
ID2D1Device* pD2DDevice = nullptr;
ID2D1DeviceContext* pD2DContext = nullptr;
IDWriteFactory* pIDWriteFactory = nullptr;
IWICImagingFactory* pIWICFactory = nullptr;

GraphicsContextManager::GraphicsContextManager() {}

GraphicsContextManager* GraphicsContextManager::Instance() {
	static GraphicsContextManager* m_Instance = new GraphicsContextManager();
	return m_Instance;
}

bool CreateD3DContext(HWND hwnd)
{
	HRESULT hr = S_OK;

	// ============================================================
	// 1. 创建 D3D11 设备（带 BGRA 支持）
	// ============================================================
	UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0
	};

	hr = D3D11CreateDevice(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		createDeviceFlags,
		featureLevels,
		ARRAYSIZE(featureLevels),
		D3D11_SDK_VERSION,
		&g_pD3DDevice,
		nullptr,
		&g_pD3DContext
	);
	if (FAILED(hr)) {
		hr = D3D11CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_WARP,
			nullptr,
			createDeviceFlags,
			featureLevels,
			ARRAYSIZE(featureLevels),
			D3D11_SDK_VERSION,
			&g_pD3DDevice,
			nullptr,
			nullptr
		);
		if (FAILED(hr)) return false;
	}

	// 设备会被两个线程同时用：渲染线程（出图桥 CopyResource/Present）
	// 和界面线程（加载框的 D2D 上下文 EndDraw 会 flush 到同一个立即上下文）。
	// D3D11 立即上下文默认不是线程安全的，这里打开内部串行化。
	{
		ID3D11Multithread* multithread = nullptr;
		if (SUCCEEDED(g_pD3DContext->QueryInterface(IID_PPV_ARGS(&multithread))))
		{
			multithread->SetMultithreadProtected(TRUE);
			multithread->Release();
		}
	}

	// 创建交换链

	RECT rect = WINDOWHANDLEMANAGER->GetRect();
	DXGI_SWAP_CHAIN_DESC1 scDesc = {};
	scDesc.Width = rect.right - rect.left;
	scDesc.Height = rect.bottom - rect.top;
	scDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	scDesc.Stereo = FALSE;
	scDesc.SampleDesc.Count = 1;
	scDesc.SampleDesc.Quality = 0;
	scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	scDesc.BufferCount = 2;
	scDesc.Scaling = DXGI_SCALING_STRETCH;
	scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	scDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	scDesc.Flags = 0;

	IDXGIDevice2* pDXGIDevice2 = nullptr;
	hr = g_pD3DDevice->QueryInterface(IID_PPV_ARGS(&pDXGIDevice2));
	if (FAILED(hr)) return false;

	IDXGIAdapter* pAdapter = nullptr;
	hr = pDXGIDevice2->GetAdapter(&pAdapter);
	pDXGIDevice2->Release();
	if (FAILED(hr)) return false;

	IDXGIFactory2* pFactory = nullptr;
	hr = pAdapter->GetParent(IID_PPV_ARGS(&pFactory));
	pAdapter->Release();
	if (FAILED(hr)) return false;

	// 支持就开 tearing（不限帧时不被 vsync 卡住），不支持就普通 flip
	{
		IDXGIFactory5* pFactory5 = nullptr;
		BOOL allow = FALSE;
		if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&pFactory5))))
		{
			if (SUCCEEDED(pFactory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow))))
				g_bAllowTearing = allow == TRUE;
			pFactory5->Release();
		}
	}
	scDesc.Flags = g_bAllowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

	IDXGISwapChain1* pSwapChain1 = nullptr;
	hr = pFactory->CreateSwapChainForHwnd(
		g_pD3DDevice,
		hwnd,
		&scDesc,
		nullptr,
		nullptr,
		&pSwapChain1
	);
	pFactory->Release();
	if (FAILED(hr)) return false;

	g_pSwapChain = pSwapChain1;

	return true;
}

bool GraphicsContextManager::InitSharedGraphicsContext()
{
	static bool init = false;
	if (init) return true;

	HRESULT hr = S_OK;

	if (!CreateD3DContext(WINDOWHANDLEMANAGER->GetHwnd()))
		return false;

	// 2D 部分归 Game：多线程工厂，加载框在界面线程画，
	// 和渲染线程对同一设备的使用互不踩脚。
	D2D1_FACTORY_OPTIONS options{};
	hr = D2D1CreateFactory(
		D2D1_FACTORY_TYPE_MULTI_THREADED,
		__uuidof(ID2D1Factory1),
		&options,
		reinterpret_cast<void**>(&pD2DFactory)
	);
	if (FAILED(hr)) return false;

	IDXGIDevice* pDXGIDevice = nullptr;
	hr = g_pD3DDevice->QueryInterface(IID_PPV_ARGS(&pDXGIDevice));
	if (FAILED(hr)) return false;

	hr = pD2DFactory->CreateDevice(pDXGIDevice, &pD2DDevice);
	pDXGIDevice->Release();
	if (FAILED(hr) || !pD2DDevice) return false;

	if (FAILED(pD2DDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &pD2DContext)))
		return false;

	if (pIDWriteFactory == nullptr) {
		DWriteCreateFactory(
			DWRITE_FACTORY_TYPE_SHARED,
			__uuidof(IDWriteFactory),
			reinterpret_cast<IUnknown**>(&pIDWriteFactory)
		);
	}

	if (pIWICFactory == nullptr) {
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		CoCreateInstance(
			CLSID_WICImagingFactory,
			NULL,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&pIWICFactory)
		);
	}

	init = true;
	return true;
}

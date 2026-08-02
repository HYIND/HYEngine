#include "RenderEngine/D2DTools.h"

//ID2D1Factory* pD2DFactory;
//ID2D1HwndRenderTarget* pRenderTarget;
//IWICImagingFactory* pIWICFactory;
//IDWriteFactory* pIDWriteFactory;

ID2D1Factory1* pD2DFactory = nullptr;			// 升级到 1.1
ID2D1DeviceContext* pRenderTarget = nullptr;	// 升级到 DeviceContext
IWICImagingFactory* pIWICFactory = nullptr;
IDWriteFactory* pIDWriteFactory = nullptr;

IDXGISwapChain1* g_pSwapChain = nullptr;
ID3D11Device* g_pD3DDevice = nullptr;			// 新增：用于 OpenGL 互操作
ID3D11DeviceContext* g_pD3DContext = nullptr;

void Need() {
	if (NULL == pIWICFactory)
	{
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		CoCreateInstance(
			CLSID_WICImagingFactory,
			NULL,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&pIWICFactory)
		);
	}
}

ID2D1Bitmap* LoadResourceBitmap(
	HINSTANCE hinstance,
	ID2D1RenderTarget* pRenderTarget,
	LPCWSTR resourceType, LPCWSTR resourceName)
{
	Need();

	IWICBitmapDecoder* pDecoder = NULL;
	IWICBitmapFrameDecode* pSource = NULL;
	IWICStream* pStream = NULL;
	IWICFormatConverter* pConverter = NULL;
	//IWICBitmapScaler* pScaler = NULL;

	HRSRC imageResHandle = NULL;
	HGLOBAL imageResDataHandle = NULL;

	void* pImageFile = NULL;
	DWORD imageFileSize = 0;

	// Locate the resource.
	imageResHandle = FindResource((HMODULE)hinstance, resourceName, resourceType);
	HRESULT hr = imageResHandle ? S_OK : E_FAIL;
	if (SUCCEEDED(hr))
	{
		// Load the resource.
		imageResDataHandle = LoadResource(hinstance, imageResHandle);

		hr = imageResDataHandle ? S_OK : E_FAIL;
	}
	if (SUCCEEDED(hr))
	{
		// Lock it to get a system memory pointer.
		pImageFile = LockResource(imageResDataHandle);

		hr = pImageFile ? S_OK : E_FAIL;
	}
	if (SUCCEEDED(hr))
	{
		// Calculate the size.
		imageFileSize = SizeofResource(hinstance, imageResHandle);

		hr = imageFileSize ? S_OK : E_FAIL;

	}
	if (SUCCEEDED(hr))
	{
		// Create a WIC stream to map onto the memory.
		hr = pIWICFactory->CreateStream(&pStream);
	}
	if (SUCCEEDED(hr))
	{
		// Initialize the stream with the memory pointer and size.
		hr = pStream->InitializeFromMemory(
			reinterpret_cast<BYTE*>(pImageFile),
			imageFileSize
		);
	}
	if (SUCCEEDED(hr))
	{
		// Create a decoder for the stream.
		hr = pIWICFactory->CreateDecoderFromStream(
			pStream,
			NULL,
			WICDecodeMetadataCacheOnLoad,
			&pDecoder
		);
	}
	if (SUCCEEDED(hr))
	{
		UINT count;
		hr = pDecoder->GetFrameCount(&count);
		// Create the initial frame.
		hr = pDecoder->GetFrame(0, &pSource);
	}
	if (SUCCEEDED(hr))
	{
		// Convert the image format to 32bppPBGRA
		// (DXGI_FORMAT_B8G8R8A8_UNORM + D2D1_ALPHA_MODE_PREMULTIPLIED).
		hr = pIWICFactory->CreateFormatConverter(&pConverter);
	}

	ID2D1Bitmap* pBitmap = nullptr;

	if (SUCCEEDED(hr))
	{
		hr = pConverter->Initialize(
			pSource,
			GUID_WICPixelFormat32bppPBGRA,
			WICBitmapDitherTypeNone,
			NULL,
			0.f,
			WICBitmapPaletteTypeMedianCut
		);
		if (SUCCEEDED(hr))
		{
			//create a Direct2D bitmap from the WIC bitmap.
			hr = pRenderTarget->CreateBitmapFromWicBitmap(
				pConverter,
				NULL,
				&pBitmap
			);

		}
		SafeRelease(pDecoder);
		SafeRelease(pSource);
		SafeRelease(pStream);
		SafeRelease(pConverter);
		//SafeRelease(pScaler);

		return pBitmap;
	}
	return nullptr;
}

ID2D1Bitmap* Loadbitmap(
	ID2D1RenderTarget* pRenderTarget,
	LPCTSTR pszResource)
{
	Need();

	HRESULT hr = S_OK;
	IWICStream* pStream = NULL;
	//IWICBitmapScaler* pScaler = NULL;
	IWICBitmapDecoder* pDecoder = NULL;
	IWICBitmapFrameDecode* pSource = NULL;
	IWICFormatConverter* pConverter = NULL;

	hr = pIWICFactory->CreateDecoderFromFilename(
		pszResource,
		NULL,
		GENERIC_READ,
		WICDecodeMetadataCacheOnLoad,
		&pDecoder
	);

	if (SUCCEEDED(hr))
	{
		// Create the initial frame.
		UINT count;
		hr = pDecoder->GetFrameCount(&count);
		hr = pDecoder->GetFrame(0, &pSource);
	}

	if (SUCCEEDED(hr))
	{
		hr = pIWICFactory->CreateFormatConverter(&pConverter);
	}

	if (SUCCEEDED(hr))
	{
		hr = pConverter->Initialize(
			pSource,
			GUID_WICPixelFormat32bppPBGRA,
			WICBitmapDitherTypeNone,
			NULL,
			0.f,
			WICBitmapPaletteTypeMedianCut
		);
	}

	ID2D1Bitmap* pBitmap = nullptr;
	if (SUCCEEDED(hr))
	{
		// Create a Direct2D bitmap from the WIC bitmap.
		hr = pRenderTarget->CreateBitmapFromWicBitmap(
			pConverter,
			NULL,
			&pBitmap
		);
	}

	SafeRelease(pDecoder);
	SafeRelease(pSource);
	SafeRelease(pStream);
	SafeRelease(pConverter);
	//SafeRelease(pScaler);

	return pBitmap;
}

bool LoadResourceGIF(
	HINSTANCE hinstance,
	ID2D1RenderTarget* pRenderTarget,
	LPCWSTR resourceType, LPCWSTR resourceName,
	UINT& out_totalFrameCount, IWICBitmapDecoder*& out_pDecoder, IWICStream*& out_pStream
)
{
	Need();

	IWICBitmapDecoder* pDecoder = NULL;
	IWICStream* pStream = NULL;
	//IWICBitmapScaler* pScaler = NULL;

	HRSRC imageResHandle = NULL;
	HGLOBAL imageResDataHandle = NULL;

	void* pImageFile = NULL;
	DWORD imageFileSize = 0;

	// Locate the resource.
	imageResHandle = FindResource((HMODULE)hinstance, resourceName, resourceType);
	HRESULT hr = imageResHandle ? S_OK : E_FAIL;
	if (SUCCEEDED(hr))
	{
		// Load the resource.
		imageResDataHandle = LoadResource(hinstance, imageResHandle);

		hr = imageResDataHandle ? S_OK : E_FAIL;
	}
	if (SUCCEEDED(hr))
	{
		// Lock it to get a system memory pointer.
		pImageFile = LockResource(imageResDataHandle);

		hr = pImageFile ? S_OK : E_FAIL;
	}
	if (SUCCEEDED(hr))
	{
		// Calculate the size.
		imageFileSize = SizeofResource(hinstance, imageResHandle);

		hr = imageFileSize ? S_OK : E_FAIL;

	}
	if (SUCCEEDED(hr))
	{
		// Create a WIC stream to map onto the memory.
		hr = pIWICFactory->CreateStream(&pStream);
	}
	if (SUCCEEDED(hr))
	{
		// Initialize the stream with the memory pointer and size.
		hr = pStream->InitializeFromMemory(
			reinterpret_cast<BYTE*>(pImageFile),
			imageFileSize
		);
	}
	if (SUCCEEDED(hr))
	{
		//Create a decoder for the stream.
		hr = pIWICFactory->CreateDecoderFromStream(
			pStream,
			&GUID_ContainerFormatGif,
			WICDecodeMetadataCacheOnLoad,
			&pDecoder
		);
	}

	if (SUCCEEDED(hr))
	{
		UINT count;
		GUID pguidContainerFormat;
		hr = pDecoder->GetContainerFormat(&pguidContainerFormat);
		hr = pDecoder->GetFrameCount(&count);

		if (SUCCEEDED(hr))
		{
			out_totalFrameCount = count;
			out_pDecoder = pDecoder;
			out_pStream = pStream;
			return true;
		}
	}

	SafeRelease(pDecoder)
		SafeRelease(pStream)

		return false;
}

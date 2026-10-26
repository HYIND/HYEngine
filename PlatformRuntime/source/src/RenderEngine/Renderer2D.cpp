#include "RenderEngine/Renderer2D.h"

#include "RenderEngine/D2DTools.h"
#include "Helper/Tools.h"

#include <algorithm>
#include <string>

namespace Render
{
	Renderer2D::Renderer2D()
	{
		_frames = std::make_shared<TripleBuffer<std::shared_ptr<Render2DFrameData>>>();
		for (int i = 0; i < 3; ++i)
			_frames->setInitialValue(i, std::make_shared<Render2DFrameData>());
	}

	Renderer2D::~Renderer2D()
	{
		Shutdown();
	}

	bool Renderer2D::Initialize(ID3D11Device* device, IDXGISwapChain1* swapChain, std::string* error)
	{
		Shutdown();

		if (!device || !swapChain)
		{
			if (error)
				*error = "Renderer2D 初始化需要 D3D11 设备与交换链";
			return false;
		}

		_device = device;
		_swapChain = swapChain;

		HRESULT hr = S_OK;
		hr = D2D1CreateFactory(
			D2D1_FACTORY_TYPE_MULTI_THREADED,
			__uuidof(ID2D1Factory1),
			nullptr,
			reinterpret_cast<void**>(&_factory));
		if (FAILED(hr) || !_factory)
		{
			if (error)
				*error = "创建 D2D 工厂失败";
			return false;
		}

		// 旧工具函数（D2DTools / D2DWrapper）仍然直接用这两个全局变量，这里给它们指向引擎自己的设备。
		pD2DFactory = _factory;

		IDXGIDevice* dxgiDevice = nullptr;
		hr = _device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
		if (FAILED(hr))
		{
			if (error)
				*error = "D3D11 设备不是 DXGI 设备";
			return false;
		}

		hr = _factory->CreateDevice(dxgiDevice, &_d2dDevice);
		dxgiDevice->Release();
		if (FAILED(hr))
		{
			if (error)
				*error = "创建 D2D 设备失败";
			return false;
		}

		hr = _d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &_target);
		if (FAILED(hr))
		{
			if (error)
				*error = "创建 D2D 设备上下文失败";
			return false;
		}

		// 兼容仍在直接用全局目标的老代码（D2DTools / D2DWrapper 那批工具函数）。
		pRenderTarget = _target;

		return ResizeTarget(error);
	}

	void Renderer2D::Shutdown()
	{
		if (_target)
			_target->SetTarget(nullptr);

		if (_targetBitmap)
		{
			_targetBitmap->Release();
			_targetBitmap = nullptr;
		}

		if (_target)
		{
			_target->Release();
			_target = nullptr;
		}

		if (_redBrush)
		{
			_redBrush->Release();
			_redBrush = nullptr;
		}

		if (_d2dDevice)
		{
			_d2dDevice->Release();
			_d2dDevice = nullptr;
		}

		if (_factory)
		{
			_factory->Release();
			_factory = nullptr;
		}

		// 断开给旧工具函数的全局桥，避免它们持着已释放的指针。
		pD2DFactory = nullptr;
		pRenderTarget = nullptr;

		_device = nullptr;
		_swapChain = nullptr;
	}

	bool Renderer2D::ResizeTarget(std::string* error)
	{
		if (!_target || !_swapChain)
		{
			if (error)
				*error = "Renderer2D 尚未初始化";
			return false;
		}

		if (_targetBitmap)
		{
			_target->SetTarget(nullptr);
			_targetBitmap->Release();
			_targetBitmap = nullptr;
		}

		ID3D11Texture2D* backBuffer = nullptr;
		HRESULT hr = _swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
		if (FAILED(hr))
		{
			if (error)
				*error = "取交换链后台缓冲失败";
			return false;
		}

		IDXGISurface* surface = nullptr;
		hr = backBuffer->QueryInterface(IID_PPV_ARGS(&surface));
		backBuffer->Release();
		if (FAILED(hr))
		{
			if (error)
				*error = "后台缓冲不是 DXGI 表面";
			return false;
		}

		D2D1_BITMAP_PROPERTIES1 properties = {};
		properties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
		properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
		properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

		hr = _target->CreateBitmapFromDxgiSurface(surface, &properties, &_targetBitmap);
		surface->Release();
		if (FAILED(hr))
		{
			if (error)
				*error = "从后台缓冲创建 D2D 位图失败";
			return false;
		}

		_target->SetTarget(_targetBitmap);
		return true;
	}

	void Renderer2D::Clear(float r, float g, float b, float a)
	{
		if (!_target)
			return;

		_target->BeginDraw();
		_target->Clear(D2D1::ColorF(r, g, b, a));
		_target->EndDraw();
	}

	void Renderer2D::RenderFrame()
	{
		if (!_target || !_frames)
			return;

		// 立即同步：取这一帧的 2D 数据、马上画。没有队列，也没有自己的线程。
		auto& frame = _frames->acquireReadBuffer();
		if (frame)
			DrawContexts(frame->D2D_Contexts);
	}

	bool Renderer2D::Present()
	{
		if (!_swapChain)
			return false;

		return SUCCEEDED(_swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING));
	}

	void Renderer2D::DrawContexts(std::vector<std::shared_ptr<D2DRenderContext::RenderContext>>& contexts)
	{
		if (!_target || contexts.empty())
			return;

		std::sort(contexts.begin(), contexts.end(),
			[](const std::shared_ptr<D2DRenderContext::RenderContext>& a, const std::shared_ptr<D2DRenderContext::RenderContext>& b)
			{
				if (!a) return false;
				if (!b) return true;

				if (a->layer != b->layer)
					return a->layer < b->layer;
				return a->internalZOrder < b->internalZOrder;
			});

		_target->BeginDraw();
		for (auto& context : contexts)
		{
			if (!context || !context->data)
				continue;

			switch (context->type)
			{
			case D2DRenderContext::RenderContextType::Sprite:
				processSprite(std::static_pointer_cast<D2DRenderContext::SpriteRenderData>(context->data));
				break;
			case D2DRenderContext::RenderContextType::GIFAnimation:
				processGIFAnimation(std::static_pointer_cast<D2DRenderContext::GIFAnimationRenderData>(context->data));
				break;
			case D2DRenderContext::RenderContextType::DebugLine:
				processDebugLines(std::static_pointer_cast<D2DRenderContext::DebugLineRenderData>(context->data));
				break;
			default:
				break;
			}
		}
		_target->EndDraw();
	}

	glm::vec2 Renderer2D::MapPosToRenderPos(const glm::vec2& mapPos) const
	{
		// 映射到目标位图自身的像素坐标。窗口尺寸属于宿主，引擎侧不反查窗口矩形。
		const D2D1_SIZE_U pixelSize = _targetBitmap ? _targetBitmap->GetPixelSize() : D2D1::SizeU(0, 0);
		const float renderWidth = static_cast<float>(pixelSize.width);
		const float renderHeight = static_cast<float>(pixelSize.height);

		const float mapWidth = static_cast<float>(MapBoundary::right - MapBoundary::left);
		const float mapHeight = static_cast<float>(MapBoundary::bottom - MapBoundary::top);
		if (renderWidth <= 0.f || renderHeight <= 0.f || mapWidth <= 0.f || mapHeight <= 0.f)
			return mapPos;

		const float normalizedX = (mapPos.x - static_cast<float>(MapBoundary::left)) / mapWidth;
		const float normalizedY = (mapPos.y - static_cast<float>(MapBoundary::top)) / mapHeight;

		return glm::vec2(normalizedX * renderWidth, normalizedY * renderHeight);
	}

	void Renderer2D::processSprite(std::shared_ptr<D2DRenderContext::SpriteRenderData> data)
	{
		if (!data || !data->bitmap)
			return;
		if (data->width <= 0 || data->height <= 0)
			return;
		if (data->opacity <= 0)
			return;

		const float x1 = data->x - data->width / 2.f;
		const float y1 = data->y - data->height / 2.f;
		const float x2 = data->x + data->width / 2.f;
		const float y2 = data->y + data->height / 2.f;

		const glm::vec2 pos1 = MapPosToRenderPos(glm::vec2(x1, y1));
		const glm::vec2 pos2 = MapPosToRenderPos(glm::vec2(x2, y2));
		const glm::vec2 center = MapPosToRenderPos(glm::vec2(data->x, data->y));

		_target->SetTransform(D2D1::Matrix3x2F::Rotation(data->rotation, D2D1::Point2F(center.x, center.y)));
		_target->DrawBitmap(data->bitmap, D2D1::RectF(pos1.x, pos1.y, pos2.x, pos2.y), data->opacity);
		_target->SetTransform(D2D1::Matrix3x2F::Identity());
	}

	void Renderer2D::processGIFAnimation(std::shared_ptr<D2DRenderContext::GIFAnimationRenderData> data)
	{
		if (!data || !data->gifInfo)
			return;

		const UINT frameCount = data->gifInfo->getFrameCount();
		if (frameCount == 0 || data->giftotalTime <= 0.f)
			return;
		if (data->width <= 0 || data->height <= 0)
			return;

		const int64_t currentTime = Tool::GetTimestampMilliseconds();
		if (data->loopCount > 0 && currentTime > data->startTime + data->loopCount * data->giftotalTime)
			return;
		if (data->opacity <= 0.f)
			return;

		const float x1 = data->x - data->width / 2.f;
		const float y1 = data->y - data->height / 2.f;
		const float x2 = data->x + data->width / 2.f;
		const float y2 = data->y + data->height / 2.f;

		const glm::vec2 pos1 = MapPosToRenderPos(glm::vec2(x1, y1));
		const glm::vec2 pos2 = MapPosToRenderPos(glm::vec2(x2, y2));

		const UINT index = static_cast<UINT>((currentTime - data->startTime) / (data->giftotalTime / frameCount)) % frameCount;
		ID2D1Bitmap* bitmap = data->gifInfo->getFrame(index);
		if (!bitmap)
			return;

		const glm::vec2 center = MapPosToRenderPos(glm::vec2(data->x, data->y));
		_target->SetTransform(D2D1::Matrix3x2F::Rotation(data->rotation, D2D1::Point2F(center.x, center.y)));
		_target->DrawBitmap(bitmap, D2D1::RectF(pos1.x, pos1.y, pos2.x, pos2.y), data->opacity);
		_target->SetTransform(D2D1::Matrix3x2F::Identity());
	}

	void Renderer2D::processDebugLines(std::shared_ptr<D2DRenderContext::DebugLineRenderData> data)
	{
		if (!data)
			return;

		if (!_redBrush)
			_target->CreateSolidColorBrush(D2D1::ColorF(1, 0, 0, 1), &_redBrush);

		const glm::vec2 pos1 = MapPosToRenderPos(data->line_pos1);
		const glm::vec2 pos2 = MapPosToRenderPos(data->line_pos2);

		_target->DrawLine(
			D2D1::Point2F(pos1.x, pos1.y),
			D2D1::Point2F(pos2.x, pos2.y),
			_redBrush);
	}
}

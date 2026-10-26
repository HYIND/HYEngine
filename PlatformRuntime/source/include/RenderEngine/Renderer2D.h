#pragma once

#include <glm/glm.hpp>

#include "RenderEngine/D2DRenderContext.h"
#include "RenderEngine/RenderFrameManager.h"
#include "Helper/TripleBuffer.h"

#include <memory>
#include <string>
#include <vector>

struct ID3D11Device;
struct IDXGISwapChain1;

// 地图坐标空间（俯视地图的虚拟坐标），2D 合成时按目标位图尺寸缩放到像素。
namespace MapBoundary
{
	inline int left = 0;
	inline int right = 1000;
	inline int top = 0;
	inline int bottom = 600;
}

namespace Render
{
	// 引擎的 2D 能力：持有 D2D 设备上下文与后台缓冲目标位图，
	// 消费 D2DRenderContext::RenderContext 列表完成合成。
	// 窗口与交换链归宿主，这里只接句柄，不做窗口管理。
	class Renderer2D
	{
	public:
		// 2D 自己的帧数据通道：场景线程写，宿主渲染线程取，和 3D 的三缓冲互不相干。
		using Render2DFrameBufferPtr = std::shared_ptr<TripleBuffer<std::shared_ptr<Render::Render2DFrameData>>>;

		Renderer2D();
		~Renderer2D();

		Renderer2D(const Renderer2D&) = delete;
		Renderer2D& operator=(const Renderer2D&) = delete;

		// device / swapChain 由宿主创建并持有生命周期，这里只借用。
		bool Initialize(ID3D11Device* device, IDXGISwapChain1* swapChain, std::string* error = nullptr);
		void Shutdown();

		// 交换链尺寸变化或重新绑定后重建后台缓冲目标位图。
		bool ResizeTarget(std::string* error = nullptr);

		bool IsReady() const { return _target != nullptr; }

		ID2D1DeviceContext* GetTarget() const { return _target; }
		// D2D 工厂归引擎：宿主若要给自己另开的窗口建 RenderTarget，从这里取。
		ID2D1Factory1* GetFactory() const { return _factory; }
		ID3D11Device* GetDevice() const { return _device; }
		IDXGISwapChain1* GetSwapChain() const { return _swapChain; }

		// 2D 帧数据缓冲。宿主/渲染服务不传时自己建一份。
		void SetFrameBuffer(Render2DFrameBufferPtr frames) { _frames = std::move(frames); }
		Render2DFrameBufferPtr GetFrameBuffer() const { return _frames; }

		// 立即同步渲染一帧 2D：取自己的帧数据画到当前目标（不含 Clear / Present）。
		// 2D 一般比 3D 快得多，所以这里不做异步队列，宿主调一次就画一帧。
		void RenderFrame();

		// 清屏（自带 BeginDraw / EndDraw）。
		void Clear(float r, float g, float b, float a = 1.0f);

		// 按 layer / internalZOrder 排序后画到当前目标（自带 BeginDraw / EndDraw）。
		void DrawContexts(std::vector<std::shared_ptr<D2DRenderContext::RenderContext>>& contexts);

		bool Present();

	private:
		void processSprite(std::shared_ptr<D2DRenderContext::SpriteRenderData> data);
		void processGIFAnimation(std::shared_ptr<D2DRenderContext::GIFAnimationRenderData> data);
		void processDebugLines(std::shared_ptr<D2DRenderContext::DebugLineRenderData> data);

		// 地图坐标 -> 渲染目标像素；尺寸取自目标位图，不依赖宿主窗口。
		glm::vec2 MapPosToRenderPos(const glm::vec2& mapPos) const;

		Render2DFrameBufferPtr _frames;

		ID3D11Device* _device = nullptr;
		IDXGISwapChain1* _swapChain = nullptr;

		ID2D1Factory1* _factory = nullptr;
		ID2D1Device* _d2dDevice = nullptr;
		ID2D1DeviceContext* _target = nullptr;
		ID2D1Bitmap1* _targetBitmap = nullptr;
		ID2D1SolidColorBrush* _redBrush = nullptr;
	};
}

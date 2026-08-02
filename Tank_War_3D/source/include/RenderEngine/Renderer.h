#pragma once

#include "RenderEngine/D2DTools.h"
#include "RenderEngine/RenderFrameManager.h"
#include "Helper/TripleBuffer.h"

#include "OpenGLRenderEngine/OpenGLRenderer.h"
#include "OpenGLRenderEngine/General/RenderState.h"

namespace Render
{
	class Renderer
	{

	public:
		using RenderTripleBufferPtr = std::shared_ptr<TripleBuffer<std::shared_ptr<RenderFrameData>>>;

	public:
		Renderer(ID2D1DeviceContext* rt = nullptr, RenderTripleBufferPtr sharedptr = nullptr);
		~Renderer();

		void SetRenderTarget(ID2D1DeviceContext* rt);
		void SetBuffers(RenderTripleBufferPtr buffers);

		void renderFrame(float delatTime);
		void renderD2DFrame(float delatTime, std::shared_ptr<RenderFrameData>& framedata);
		void renderOpenGLFrame(float delatTime, std::shared_ptr<RenderFrameData>& framedata);

		void InitOpenGLRender(int scr_width, int scr_height);

		int GetOpenGLWidth();
		int GetOpenGLHeight();

	private:
		void processSprite(std::shared_ptr<D2DRenderContext::SpriteRenderData> data);
		void processGIFAnimation(std::shared_ptr<D2DRenderContext::GIFAnimationRenderData> data);
		void processDebugLines(std::shared_ptr<D2DRenderContext::DebugLineRenderData> data);

	private:
		void processModel(
			RenderState& state,
			std::vector<OpenGLRender::SceneItem>& items,
			std::vector<OpenGLRender::SceneTransparentItem>& transparentItems,
			GroupMapper& itemsGroupMapper,
			GroupMapper& transparentItemsGroupMapper,
			const std::shared_ptr<OpenGLRenderContext::SceneModelRenderData>& data
		);
		void processFirstPersonModel(
			RenderState& state,
			std::vector<OpenGLRender::FirstPersonItem>& items, 
			GroupMapper& itemsGroupMapper,
			const std::shared_ptr<OpenGLRenderContext::FirstPersonRenderData>& data);

		void ConvertGLTextureToD2DBitmap();
		void ConvertGLTextureToD2DBitmap1();
		void ConvertGLTextureToD2DBitmap2();

	private:
		ID2D1DeviceContext* _renderTarget = nullptr;
		RenderTripleBufferPtr _buffers;
		ID2D1SolidColorBrush* _redBrush;
		std::shared_ptr<OpenGLRenderer> _openglRenderer;

		ID2D1Bitmap* _openGLBitmap = nullptr;
		ID2D1Bitmap1* _openGLBitmap1 = nullptr;
		bool _usebitmap1;
	};
}
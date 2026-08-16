#pragma once

#include "RenderEngine/D2DTools.h"
#include "RenderEngine/RenderFrameManager.h"
#include "Helper/TripleBuffer.h"

#include "OpenGLRenderEngine/OpenGLRenderer.h"
#include "OpenGLRenderEngine/General/RenderState.h"

namespace MapBoundary
{
	inline int left = 0;
	inline int right = 1000;
	inline int top = 0;
	inline int bottom = 600;
}

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
		std::shared_ptr<OpenGLRenderer> GetOpenGLRender();

	private:
		void processSprite(std::shared_ptr<D2DRenderContext::SpriteRenderData> data);
		void processGIFAnimation(std::shared_ptr<D2DRenderContext::GIFAnimationRenderData> data);
		void processDebugLines(std::shared_ptr<D2DRenderContext::DebugLineRenderData> data);

	private:
		void AnalysisRenderFrameData(std::shared_ptr<RenderFrameData>& framedata, RenderState& state);

		void processSceneModel(
			RenderState& state,
			OpenGLRenderObjectData::SceneRenderData& renderData,
			const std::shared_ptr<OpenGLRenderContext::SceneModelRenderData>& data
		);
		void processFirstPersonModel(
			RenderState& state,
			OpenGLRenderObjectData::FirstPersonRenderData& renderData,
			const std::shared_ptr<OpenGLRenderContext::FirstPersonRenderData>& data);

		void ConvertGLTextureToD2DBitmap();
		void ConvertGLTextureToD2DBitmap1();

	public:
		RenderOption GetOption() const;
		void SetOption(RenderOption option);

		bool GetNeedUpdateOpiton() const;
		void SetNeedUpdateOpiton(bool value);

	private:
		ID2D1DeviceContext* _renderTarget = nullptr;
		RenderTripleBufferPtr _buffers;
		ID2D1SolidColorBrush* _redBrush;
		std::shared_ptr<OpenGLRenderer> _openglRenderer;

		ID2D1Bitmap* _openGLBitmap = nullptr;
		ID2D1Bitmap1* _openGLBitmap1 = nullptr;
		bool _usebitmap1;

		RenderOption _option;
		bool _optionChange;

		bool _isOpenGLInit;
	};
}
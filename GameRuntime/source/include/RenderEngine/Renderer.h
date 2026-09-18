#pragma once

#include "RenderEngine/D2DTools.h"
#include "RenderEngine/RenderFrameManager.h"
#include "Helper/TripleBuffer.h"
#include "Helper/DoubleBuffer.h"
#include "Helper/DynamicFpsController.h"

#include "VulkanRenderEngine/VulkanRenderer.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "VulkanRenderEngine/SharedTexture.h"


namespace MapBoundary
{
	inline int left = 0;
	inline int right = 1000;
	inline int top = 0;
	inline int bottom = 600;
}

namespace Render
{
	class RenderFrameDataAnalysisHelp
	{

	public:
		static void AnalysisRenderFrameData(std::shared_ptr<RenderFrameData>& framedata, RenderState& state);

	private:
		static void processSceneModel(
			RenderState& state,
			VKRenderObjectData::SceneRenderData& renderData,
			const std::shared_ptr<VKRenderContext::SceneModelRenderData>& data
		);
		static void processFirstPersonModel(
			RenderState& state,
			VKRenderObjectData::FirstPersonRenderData& renderData,
			const std::shared_ptr<VKRenderContext::FirstPersonRenderData>& data);
	};

	class Renderer
	{
		struct EarlyProcessData
		{
			std::shared_ptr<VulkanRenderer> render;
			std::shared_ptr<RenderState> state;
			std::vector<std::shared_ptr<D2DRenderContext::RenderContext>> D2D_Contexts;
		};

	public:
		using RenderTripleBufferPtr = std::shared_ptr<TripleBuffer<std::shared_ptr<RenderFrameData>>>;

	public:
		Renderer(ID2D1DeviceContext* rt = nullptr, RenderTripleBufferPtr sharedptr = nullptr);
		~Renderer();

		void SetRenderTarget(ID2D1DeviceContext* rt);
		void SetBuffers(RenderTripleBufferPtr buffers);

		void PushFrameLoop();
		void renderFrame();
		void renderD2DFrame(std::vector<std::shared_ptr<D2DRenderContext::RenderContext>>& D2DContexts);
		void renderVulkanFrame();

		void InitVulkanRender(uint32_t scr_width, uint32_t scr_height);

		int GetOpenGLWidth();
		int GetOpenGLHeight();
		std::shared_ptr<VulkanRenderer> GetOpenGLRender();

	private:
		void processSprite(std::shared_ptr<D2DRenderContext::SpriteRenderData> data);
		void processGIFAnimation(std::shared_ptr<D2DRenderContext::GIFAnimationRenderData> data);
		void processDebugLines(std::shared_ptr<D2DRenderContext::DebugLineRenderData> data);

	public:
		RenderOption GetOption() const;
		void SetOption(RenderOption option);

		bool GetNeedUpdateOpiton() const;
		void SetNeedUpdateOpiton(bool value);

	private:
		ID2D1DeviceContext* _renderTarget = nullptr;
		RenderTripleBufferPtr _buffers;
		ID2D1SolidColorBrush* _redBrush;

		std::shared_ptr<VulkanRenderer> _vulkanRenderer;
		std::shared_ptr<SharedTexture> _sharedTexture;

		RenderOption _option;
		bool _optionChange;

		bool _isVulkanInit;

		bool _pushFrameStop;
		std::shared_ptr<std::thread> _pushFrameThread;
		TripleBuffer<EarlyProcessData> _earlyDataBuffers;

		std::shared_ptr<Texture2D> sharedTexture;

		DynamicFpsController controller;
		DynamicFpsEstimate estimate;
	};
}
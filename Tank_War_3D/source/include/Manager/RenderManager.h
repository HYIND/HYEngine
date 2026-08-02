#pragma once

#include "RenderEngine/D2DTools.h"
#include "RenderEngine/Renderer.h"
#include "Helper/DynamicFpsController.h"
#include "RenderEngine/RenderFrameManager.h"
#include "Helper/TripleBuffer.h"
#include <memory>

class RenderManager
{
public:
	using RenderFrameDataPtr = std::shared_ptr<Render::RenderFrameData>;

public:
	static RenderManager* Instance();
	~RenderManager();

	std::shared_ptr<Render::Renderer> getRenderer();
	std::shared_ptr<DynamicFpsController> getfpsController();
	std::shared_ptr<TripleBuffer<RenderFrameDataPtr>> getBufferManager();

private:
	RenderManager();

private:
	std::shared_ptr<Render::Renderer> _Renderer;
	std::shared_ptr<DynamicFpsController> _fpsController;
	std::shared_ptr<TripleBuffer<RenderFrameDataPtr>> _bufferManager;
};



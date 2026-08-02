#include "Manager/RenderManager.h"

using namespace Render;

RenderManager* RenderManager::Instance()
{
	static RenderManager* m_instance = new RenderManager();
	return m_instance;
}

RenderManager::RenderManager()
{
	_bufferManager = std::make_shared<TripleBuffer<RenderManager::RenderFrameDataPtr>>();
	_fpsController = std::make_shared<DynamicFpsController>(144);
	_Renderer = std::make_shared<Renderer>(pRenderTarget, _bufferManager);
}

RenderManager::~RenderManager()
{

}

std::shared_ptr<Renderer> RenderManager::getRenderer()
{
	return _Renderer;
}

std::shared_ptr<DynamicFpsController> RenderManager::getfpsController()
{
	return _fpsController;
}

std::shared_ptr<TripleBuffer<RenderManager::RenderFrameDataPtr>> RenderManager::getBufferManager()
{
	return _bufferManager;
}

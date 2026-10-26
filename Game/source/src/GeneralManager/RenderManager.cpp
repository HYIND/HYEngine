#include "GeneralManager/RenderManager.h"
#include "GameEngine.h"

using namespace Render;

RenderManager* RenderManager::Instance()
{
	static RenderManager* m_instance = new RenderManager();
	return m_instance;
}

RenderManager::RenderManager()
{
	_vulkanBridge = std::make_shared<VulkanPresentBridge>();
}

RenderManager::~RenderManager()
{

}

std::shared_ptr<VulkanPresentBridge> RenderManager::getVulkanBridge()
{
	return _vulkanBridge;
}

Scene3DRenderOption RenderManager::GetOption() const{
	auto* renderer = GameEngine::Instance().Runtime().GetRenderer();
	auto* renderer3D = renderer ? renderer->GetRenderer3D() : nullptr;
	return renderer3D ? renderer3D->GetOption() : Scene3DRenderOption{};
}

void RenderManager::SetOption(Scene3DRenderOption option) {
	auto* renderer = GameEngine::Instance().Runtime().GetRenderer();
	auto* renderer3D = renderer ? renderer->GetRenderer3D() : nullptr;
	if (renderer3D)
		renderer3D->SetOption(option);
}

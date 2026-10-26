#include "vkstdafx.h"
#include "RenderEngine/Renderer.h"
#include "RenderEngine/Renderer3D.h"
#include "RenderEngine/Renderer2D.h"
#include "Render/Systems/RenderSystem.h"
#include "EngineSystemPriority.h"
#include "ECSCore/World.h"
#include "RenderEngine/D2DTools.h"
#include "Helper/Tools.h"
#include "VulkanRenderEngine/VKContext.h"
#include <glm/gtc/matrix_access.hpp>
#include <memory>
#include <algorithm>
#include <ranges>

using namespace Render;

Renderer::Renderer()
	:_renderer3D(std::make_unique<Renderer3D>())
	, _renderer2D(std::make_unique<Renderer2D>())
{
}

Render::Renderer::~Renderer() = default;

void Renderer::BindRenderSystem(RenderSystem& renderSystem)
{
	renderSystem.SetTriBuffer3D(_renderer3D ? _renderer3D->GetFrameBuffer() : nullptr);
	renderSystem.SetTriBuffer2D(_renderer2D ? _renderer2D->GetFrameBuffer() : nullptr);
}

void Renderer::RegisterRenderSystem(World& world)
{
	auto& renderSystem = world.registerSystem<RenderSystem>(ToSystemPriority(EngineSystemPriority::Render));
	BindRenderSystem(renderSystem);
}

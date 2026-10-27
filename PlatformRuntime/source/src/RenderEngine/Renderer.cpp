#include "vkstdafx.h"
#include "RenderEngine/Renderer.h"
#include "RenderEngine/Renderer3D.h"
#include "RenderEngine/Renderer2D.h"
#include "RenderEngine/RendererUI.h"
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
	, _uiRender(std::make_unique<RendererUI>())
{}

Render::Renderer::~Renderer() = default;

void Renderer::SetVulkanRenderer(std::shared_ptr<VulkanRenderer> renderer)
{
	if (!_renderer3D)
		return;

	_renderer3D->SetVulkanRenderer(std::move(renderer));
}

bool Renderer::CreateRenderTarget(uint32_t width, uint32_t height)
{
	if (!_renderer3D || !_renderer3D->CreateRenderTarget(width, height))
		return false;

	return true;
}

bool Renderer::ResizeRenderTarget(uint32_t width, uint32_t height, const std::function<void()>& beforeResize)
{
	if (!_renderer3D)
		return false;

	if (!_renderer3D->ResizeRenderTarget(width, height, beforeResize))
		return false;

	return true;
}

bool Renderer::Start()
{
	return _renderer3D ? _renderer3D->Start() : false;
}

void Renderer::StopFrameLoop()
{
	if (_renderer3D)
		_renderer3D->StopFrameLoop();
}

bool Renderer::IsFrameLoopRunning() const
{
	return _renderer3D ? _renderer3D->IsFrameLoopRunning() : false;
}

bool Renderer::HasRenderer() const
{
	return _renderer3D ? _renderer3D->HasRenderer() : false;
}

bool Renderer::IsRenderTargetReady() const
{
	return _renderer3D ? _renderer3D->IsRenderTargetReady() : false;
}

uint32_t Renderer::GetRenderTargetWidth() const
{
	return _renderer3D ? _renderer3D->GetRenderTargetWidth() : 0;
}

uint32_t Renderer::GetRenderTargetHeight() const
{
	return _renderer3D ? _renderer3D->GetRenderTargetHeight() : 0;
}

void Renderer::Shutdown()
{
	if (_renderer3D)
	{
		_renderer3D->StopFrameLoop();
		_renderer3D->Shutdown();
	}
	if (_renderer2D)
		_renderer2D->Shutdown();
	if (_uiRender)
		_uiRender->Shutdown();
}

void Renderer::WaitImage(const WaitImageCallback& callback)
{
	if (!_renderer3D)
		return;

	// 3D 出帧之后才轮到 UI：先拿 3D 的纹理，再把 UI 叠上去
	_renderer3D->WaitImage([this, callback](std::shared_ptr<Texture2D> tex, std::shared_ptr<RenderState> state)
		{
			 _uiRender->Render(tex);

			// 事件是 Render 里面（ImGui 控件判定时）产生的，所以只能在这里、同一线程紧接着取。
			if (_uiEventSink)
				_uiEventSink(_uiRender->PollEvents());

			if (callback)
				callback(tex, state);
		});
}

void Renderer::SetUiInputContext(std::shared_ptr<EngineInputContext> context)
{
	if (_uiRender)
		_uiRender->SetInputContext(std::move(context));
}

void Renderer::SetUiEventSink(std::function<void(std::vector<UI::UiEvent>)> sink)
{
	_uiEventSink = std::move(sink);
}

void Renderer::BindRenderSystem(RenderSystem& renderSystem)
{
	renderSystem.SetTriBuffer3D(_renderer3D ? _renderer3D->GetFrameBuffer() : nullptr);
	renderSystem.SetTriBuffer2D(_renderer2D ? _renderer2D->GetFrameBuffer() : nullptr);
	renderSystem.SetTriBufferUI(_uiRender ? _uiRender->GetFrameBuffer() : nullptr);
}

void Renderer::RegisterRenderSystem(World& world)
{
	auto& renderSystem = world.registerSystem<RenderSystem>(ToSystemPriority(EngineSystemPriority::Render));
	BindRenderSystem(renderSystem);
}

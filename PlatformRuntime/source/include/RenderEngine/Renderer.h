#pragma once

#include "RenderEngine/D2DTools.h"
#include "RenderEngine/D2DRenderContext.h"
#include "RenderEngine/RenderFrameManager.h"
#include "RenderEngine/Renderer3D.h"
#include "RenderEngine/Renderer2D.h"

#include "VulkanRenderEngine/VulkanRenderer.h"

#include <vector>

class World;
class RenderSystem;

namespace Render
{
	class Renderer
	{
	public:
		Renderer();
		~Renderer();

		Renderer3D* GetRenderer3D() noexcept { return _renderer3D.get(); }
		const Renderer3D* GetRenderer3D() const noexcept { return _renderer3D.get(); }
		Renderer2D* GetRenderer2D() noexcept { return _renderer2D.get(); }
		const Renderer2D* GetRenderer2D() const noexcept { return _renderer2D.get(); }

		void BindRenderSystem(RenderSystem& renderSystem);
		void RegisterRenderSystem(World& world);

	private:
		std::unique_ptr<Renderer3D> _renderer3D;
		std::unique_ptr<Renderer2D> _renderer2D;
	};
}

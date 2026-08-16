#pragma once

#include "RenderGraphContext.h"
#include "TexturePool.h"
#include <unordered_map>

namespace OpenGLRenderGraph
{

	class ResourceManager
	{
	public:
		ResourceManager() = default;
		~ResourceManager() {};

	public:
		std::shared_ptr<Texture2D> TryGetTexture(const RenderGraphResource& res);
		std::shared_ptr<Texture2D> GetTexture(const RenderGraphResource& res);
		void ReleaseTexture(const RenderGraphResource& res);

		void RegisterExternalTexture(const ResourceName& name, std::shared_ptr<Texture2D> texture);
		void UnregisterExternalTexture(const ResourceName& name);
		std::shared_ptr<Texture2D> GetExternalTexture(const ResourceName& name) const;

		void CleanupIdleResource();
	private:
		TexturePool _texPool;
		std::unordered_map<ResourceName, TextureHandle> _textures;
		std::unordered_map<ResourceName, std::shared_ptr<Texture2D>> _externalTextures;
	};
}
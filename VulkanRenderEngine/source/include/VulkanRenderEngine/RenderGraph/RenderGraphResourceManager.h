#pragma once

#include "vkstdafx.h"
#include "RenderGraphContext.h"
#include "TexturePool.h"

namespace RenderGraph
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

		mutable SpinLock _texturesMutex;
		mutable SpinLock _externalTexturesMutex;
	};
}
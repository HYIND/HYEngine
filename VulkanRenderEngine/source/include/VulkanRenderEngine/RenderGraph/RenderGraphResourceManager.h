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
		std::shared_ptr<Texture2D> TryGetTexture(const RenderGraphResource& res, const std::string& prefix);
		std::shared_ptr<Texture2D> GetTexture(const RenderGraphResource& res, const std::string& prefix);
		void ReleaseTexture(const RenderGraphResource& res, const std::string& prefix);

		void CleanupIdleResource();
	private:
		TexturePool _texPool;

		std::unordered_map<ResourceName, TextureHandle> _textures;
		mutable SpinLock _texturesMutex;
	};

	class ExternalResourceManager
	{
	public:
		ExternalResourceManager() = default;
		ExternalResourceManager(const std::unordered_map<ResourceName, std::shared_ptr<Texture2D>>& textures);

		void RegisterExternalTexture(const ResourceName& name, std::shared_ptr<Texture2D> texture);
		void UnregisterExternalTexture(const ResourceName& name);
		std::shared_ptr<Texture2D> GetExternalTexture(const ResourceName& name) const;

	private:
		std::unordered_map<ResourceName, std::shared_ptr<Texture2D>> _externalTextures;
		mutable SpinLock _externalTexturesMutex;
	};
}
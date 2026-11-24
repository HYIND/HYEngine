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
		std::shared_ptr<Texture3D> TryGetVolume(const RenderGraphResource& res, const std::string& prefix);
		std::shared_ptr<Texture3D> GetVolume(const RenderGraphResource& res, const std::string& prefix);
		void ReleaseTexture(const RenderGraphResource& res, const std::string& prefix);

		// 从 other 接手一张没有前缀的资源（Persistent）：other 里有同名、且描述（尺寸 / 格式 / 采样）与 res 一致的纹理时，
		// 连同内容转到这里，返回 true；没有、描述不同或这里已经有同名资源时什么都不做
		bool AdoptTexture(ResourceManager& other, const RenderGraphResource& res);

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

	class FrameLocalResourceRecord
	{
	public:
		FrameLocalResourceRecord() = default;

		void AddTexture(const RenderGraphResource& name);
		const std::unordered_set<RenderGraphResource>& GetRes() const;

	private:
		std::unordered_set<RenderGraphResource> _textures;
		mutable SpinLock _texturesMutex;
	};
}
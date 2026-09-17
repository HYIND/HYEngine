#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderGraph/RenderGraphResourceManager.h"

using namespace RenderGraph;

static inline std::string GenerateName(const std::string& prefix, const std::string& name) {
	return !prefix.empty() ? std::format("{}-{}", prefix, name) : name;
}

std::shared_ptr<Texture2D> ResourceManager::TryGetTexture(const RenderGraphResource& res, const std::string& prefix)
{
	auto name = GenerateName(prefix, res.name);

	LockGuard guard(_texturesMutex);
	auto it = _textures.find(name);
	if (it != _textures.end())
		return _texPool.GetTexture(it->second);
	return nullptr;
}

std::shared_ptr<Texture2D> ResourceManager::GetTexture(const RenderGraphResource& res, const std::string& prefix)
{
	auto name = GenerateName(prefix, res.name);

	LockGuard guard(_texturesMutex);
	auto it = _textures.find(name);
	if (it != _textures.end())
		return _texPool.GetTexture(it->second);

	if (res.type == ResourceType::Texture)
	{
		if (auto desc = std::get_if<TextureDesc>(&res.desc))
		{
			auto texhandle = _texPool.AllocateTexture(*desc);
			_textures[name] = texhandle;
			return _texPool.GetTexture(texhandle);
		}
	}

	return nullptr;
}

void ResourceManager::ReleaseTexture(const RenderGraphResource& res, const std::string& prefix)
{
	auto name = GenerateName(prefix, res.name);

	LockGuard guard(_texturesMutex);
	auto it = _textures.find(name);
	if (it == _textures.end())
		return;
	_texPool.ReleaseTexture(it->second);
	_textures.erase(it);
}


void RenderGraph::ResourceManager::CleanupIdleResource()
{
	LockGuard guard(_texturesMutex);
	_texPool.CleanupIdleTextures();
}

RenderGraph::ExternalResourceManager::ExternalResourceManager(const std::unordered_map<ResourceName, std::shared_ptr<Texture2D>>& textures)
	:_externalTextures(textures)
{}

void ExternalResourceManager::RegisterExternalTexture(const ResourceName& name, std::shared_ptr<Texture2D> texture)
{
	LockGuard guard(_externalTexturesMutex);
	_externalTextures[name] = texture;
}

void ExternalResourceManager::UnregisterExternalTexture(const ResourceName& name)
{
	LockGuard guard(_externalTexturesMutex);
	if (_externalTextures.find(name) == _externalTextures.end())
		return;
	_externalTextures.erase(name);
}

std::shared_ptr<Texture2D> ExternalResourceManager::GetExternalTexture(const ResourceName& name) const
{
	LockGuard guard(_externalTexturesMutex);
	auto it = _externalTextures.find(name);
	if (it == _externalTextures.end())
		return nullptr;
	return it->second;
}
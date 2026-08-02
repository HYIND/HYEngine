#include "OpenGLRenderEngine/RenderGraph/RenderGraphResourceManager.h"

using namespace OpenGLRenderGraph;

std::shared_ptr<Texture2D> ResourceManager::TryGetTexture(const RenderGraphResource& res)
{
	auto ext_it = _externalTextures.find(res.name);
	if (ext_it != _externalTextures.end())
		return ext_it->second;

	auto it = _textures.find(res.name);
	if (it != _textures.end())
		return _texPool.GetTexture(it->second);
	return nullptr;
}

std::shared_ptr<Texture2D> ResourceManager::GetTexture(const RenderGraphResource& res)
{
	auto ext_it = _externalTextures.find(res.name);
	if (ext_it != _externalTextures.end())
		return ext_it->second;

	auto it = _textures.find(res.name);
	if (it != _textures.end())
		return _texPool.GetTexture(it->second);

	if (res.type == ResourceType::Texture)
	{
		if (auto desc = std::get_if<TextureDesc>(&res.desc))
		{
			auto texhandle = _texPool.AllocateTexture(*desc);
			_textures[res.name] = texhandle;
			return _texPool.GetTexture(texhandle);
		}
	}

	return nullptr;
}

void ResourceManager::ReleaseTexture(const RenderGraphResource& res)
{
	auto it = _textures.find(res.name);
	if (it == _textures.end())
		return;
	_texPool.ReleaseTexture(it->second);
	_textures.erase(it);
}

void ResourceManager::RegisterExternalTexture(const ResourceName& name, std::shared_ptr<Texture2D> texture)
{
	_externalTextures[name] = texture;
}

void ResourceManager::UnregisterExternalTexture(const ResourceName& name)
{
	if (_externalTextures.find(name) == _externalTextures.end())
		return;
	_externalTextures.erase(name);
}

std::shared_ptr<Texture2D> ResourceManager::GetExternalTexture(const ResourceName& name) const
{
	auto it = _externalTextures.find(name);
	if (it == _externalTextures.end())
		return nullptr;
	return it->second;
}
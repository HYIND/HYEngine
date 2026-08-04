#include "OpenGLRenderEngine/RenderGraph/TexturePool.h"
#include <format>
#include "Helper/Tools.h"

using namespace OpenGLRenderGraph;

// 创建资源句柄（不分配GPU内存）
TextureHandle TexturePool::AllocateTexture(const TextureDesc& desc)
{
	const auto& key = GenerateKey(desc);

	TextureHandle handle = FetchIdleTexture(key);
	if (handle)
		return handle;

	auto tex = std::make_shared<Texture2D>(desc.width, desc.height, desc.format, desc.maxLevel);
	tex->SetFiltering(desc.minFilterMode, desc.magFilterMode).SetWrapping(desc.wrapSMode, desc.wrapTMode);

	handle = TextureHandle(_nextId++, desc);
	_textures[handle] = tex;
	return handle;
}

std::shared_ptr<Texture2D> TexturePool::GetTexture(const TextureHandle& handle) const
{
	auto it = _textures.find(handle);
	if (it != _textures.end())
		return it->second;
	return std::shared_ptr<Texture2D>();
}

void TexturePool::ReleaseTexture(const TextureHandle& handle) {
	auto it = _textures.find(handle);
	if (it == _textures.end()) return;

	auto key = GenerateKey(handle.GetDesc());
	_idleTextures[key].push_back(IdleInfo{ handle ,Tool::GetTimestampSecond() });
}

TexKey TexturePool::GenerateKey(const TextureDesc& desc) const
{
	return std::format("{}_{}_{}_{}_{}_{}_{}_{}",
		desc.width, desc.height, desc.format, desc.minFilterMode, desc.magFilterMode, desc.wrapSMode, desc.wrapTMode, desc.maxLevel);
}

TextureHandle TexturePool::FetchIdleTexture(const TexKey& key)
{
	auto it = _idleTextures.find(key);
	if (it == _idleTextures.end() || it->second.empty())
		return TextureHandle();

	auto& deque = it->second;
	auto info = deque.back();
	deque.pop_back();

	return info.handle;
}

void TexturePool::CleanupIdleTextures()
{
	auto CleanupDeque = [&](int64_t expireTime, std::deque<IdleInfo>& deque) -> void
		{

			int64_t currentTime = Tool::GetTimestampSecond();

			while (!deque.empty())
			{
				auto& info = deque.front();
				if (info.timeStamp < expireTime)
				{
					_textures.erase(info.handle);
					deque.pop_front();
				}
				else
					break;
			}
		};

	int64_t expireTime = Tool::GetTimestampSecond() - _cleanupThreshold;

	for (auto& pair : _idleTextures) {
		auto& deque = pair.second;
		CleanupDeque(expireTime, deque);
	}
}

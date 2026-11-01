#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderGraph/TexturePool.h"

using namespace RenderGraph;

RenderGraph::TexturePool::TexturePool() :
	_nextId(1)
{}

RenderGraph::TexturePool::~TexturePool()
{}

// 分配GPU内存
TextureHandle TexturePool::AllocateTexture(const TextureDesc& desc)
{
	const auto& key = GenerateKey(desc);

	TextureHandle handle = FetchIdleTexture(key);
	if (handle)
		return handle;

	TextureConfig config;
	config.minFilter = desc.minFilter;
	config.magFilter = desc.magFilter;
	config.wrapU = desc.wrapU;
	config.wrapV = desc.wrapV;
	config.wrapW = desc.wrapW;

	TextureSlot slot;
	if (desc.IsVolume())
		slot.Set(std::make_shared<Texture3D>(desc.width, desc.height, desc.depth, desc.format, config, desc.maxLevel));
	else
		slot.Set(std::make_shared<Texture2D>(desc.width, desc.height, desc.format, config, desc.maxLevel));

	handle = TextureHandle(_nextId++, desc);
	_textures[handle] = slot;
	return handle;
}

std::shared_ptr<Texture2D> TexturePool::GetTexture(const TextureHandle& handle) const
{
	auto it = _textures.find(handle);
	if (it != _textures.end())
		return it->second.As<Texture2D>();
	return std::shared_ptr<Texture2D>();
}

std::shared_ptr<Texture3D> TexturePool::GetVolume(const TextureHandle& handle) const
{
	auto it = _textures.find(handle);
	if (it != _textures.end())
		return it->second.As<Texture3D>();
	return std::shared_ptr<Texture3D>();
}

void TexturePool::ReleaseTexture(const TextureHandle& handle) {
	auto it = _textures.find(handle);
	if (it == _textures.end()) return;

	auto key = GenerateKey(handle.GetDesc());
	_idleTextures[key].push_back(IdleInfo{ handle ,Tool::GetTimestampSecond() });
}

TexKey TexturePool::GenerateKey(const TextureDesc& desc) const
{
	// depth / wrapW 必须进 key，否则 2D 与 3D、不同体积尺寸会互相复用错资源
	if (desc.isVariable)
		return std::format("__Variable_{}_{}_{}_{}_{}_{}_{}_{}",
			(uint32_t)desc.format, (uint32_t)desc.minFilter, (uint32_t)desc.magFilter, (uint32_t)desc.wrapU, (uint32_t)desc.wrapV, (uint32_t)desc.wrapW, desc.maxLevel, desc.depth);
	else
		return std::format("{}_{}_{}_{}_{}_{}_{}_{}_{}_{}",
			desc.width, desc.height, desc.depth, (uint32_t)desc.format, (uint32_t)desc.minFilter, (uint32_t)desc.magFilter, (uint32_t)desc.wrapU, (uint32_t)desc.wrapV, (uint32_t)desc.wrapW, desc.maxLevel);
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

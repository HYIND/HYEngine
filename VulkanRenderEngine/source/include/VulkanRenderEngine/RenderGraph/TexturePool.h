#pragma once

#include "vkstdafx.h"
#include "../Base/Texture2D.h";
#include "../Base/Texture3D.h";
#include "RenderGraphContext.h"

namespace RenderGraph
{

	using TexKey = std::string;

	class TexturePool
	{
	public:
		TexturePool();
		~TexturePool();

		TextureHandle AllocateTexture(const TextureDesc& desc);
		std::shared_ptr<Texture2D> GetTexture(const TextureHandle& handle) const;
		std::shared_ptr<Texture3D> GetVolume(const TextureHandle& handle) const;
		void ReleaseTexture(const TextureHandle& handle);

		void CleanupIdleTextures();

	private:
		TexKey GenerateKey(const TextureDesc& desc) const;
		TextureHandle FetchIdleTexture(const TexKey& key);

	private:
		struct IdleInfo
		{
			TextureHandle handle;
			int64_t timeStamp = 0;
			bool operator==(const IdleInfo& other) const { return handle == other.handle; }
		};
	private:
		uint32_t _nextId;
		std::unordered_map<TextureHandle, TextureSlot> _textures;						// 分配过的所有资源
		std::unordered_map<TexKey, std::deque<IdleInfo>> _idleTextures;					// 回收回来的资源
		int64_t _cleanupThreshold = 10;
	};

}

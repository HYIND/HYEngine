#pragma once

#include "../Base/Texture2D.h";
#include "RenderGraphContext.h"
#include <stdint.h>
#include <iostream>
#include <unordered_map>
#include <deque>

namespace OpenGLRenderGraph
{

	using TexKey = std::string;

	class TexturePool
	{
	public:
		TexturePool() : _nextId(1) {}
		TextureHandle AllocateTexture(const TextureDesc& desc);
		std::shared_ptr<Texture2D> GetTexture(const TextureHandle& handle) const;
		void ReleaseTexture(const TextureHandle& handle);

		void CleanupIdleTextures();

	private:
		TexKey GenerateKey(const TextureDesc& desc) const;
		TextureHandle FetchIdleTexture(const TexKey& key);

	public:
		struct IdleInfo
		{
			TextureHandle handle;
			int64_t timeStamp = 0;
			bool operator==(const IdleInfo& other) const { return handle == other.handle; }
		};

	private:
		uint32_t _nextId;
		std::unordered_map<TextureHandle, std::shared_ptr<Texture2D>> _textures;		// 分配过的所有资源
		std::unordered_map<TexKey, std::deque<IdleInfo>> _idleTextures;					// 回收回来的资源
		int64_t _cleanupThreshold = 10;
	};

}

#pragma once

#include <GL\glew.h>
#include <stdint.h>
#include <iostream>
#include <vector>
#include <variant>
#include "../Base/Texture2D.h"

namespace OpenGLRenderGraph
{

	struct TextureDesc {
		int width = 0;
		int height = 0;

		GLenum format = GL_RGBA;

		GLenum filterMode = GL_LINEAR;
		GLenum wrapMode = GL_CLAMP_TO_EDGE;
	};

	class TextureHandle
	{
	public:
		TextureHandle() : _id(0) {}
		explicit TextureHandle(uint32_t id, const TextureDesc& desc) : _id(id), _desc(desc) {}
		uint32_t GetID() const { return _id; }
		TextureDesc GetDesc() const { return _desc; }
		bool IsValid() const { return _id != 0; }
		bool operator==(const TextureHandle& other) const { return _id == other._id; }
		explicit operator bool() const { return _id > 0; }

	private:
		uint32_t _id = 0;
		TextureDesc _desc;
	};

	struct PassContext
	{
		std::string passName;
		GLuint renderTargetFBO = 0;

		std::vector<std::shared_ptr<Texture2D>> inputTextures;
		std::vector<std::shared_ptr<Texture2D>> optionInputTextures;
		std::vector<std::shared_ptr<Texture2D>> outputTextures;
		std::vector<std::shared_ptr<Texture2D>> tempTextures;
		std::vector<std::shared_ptr<Texture2D>> persitentTextures;
		std::vector<std::shared_ptr<Texture2D>> externalTextures;

		const std::string& GetName() const { return passName; };
		GLuint GetRenderTargetFBO() const { return renderTargetFBO; };
		std::shared_ptr<Texture2D> GetInput(int idx) const { return idx >= 0 && idx < inputTextures.size() ? inputTextures[idx] : nullptr; };
		std::shared_ptr<Texture2D> GetOptionalInput(int idx) const { return idx >= 0 && idx < optionInputTextures.size() ? optionInputTextures[idx] : nullptr; };
		std::shared_ptr<Texture2D> GetOutput(int idx) const { return idx >= 0 && idx < outputTextures.size() ? outputTextures[idx] : nullptr; };
		std::shared_ptr<Texture2D> GetTemp(int idx) const { return idx >= 0 && idx < tempTextures.size() ? tempTextures[idx] : nullptr; };
		std::shared_ptr<Texture2D> GetPersitent(int idx) const { return idx >= 0 && idx < persitentTextures.size() ? persitentTextures[idx] : nullptr; };
		std::shared_ptr<Texture2D> GetExternal(int idx) const { return idx >= 0 && idx < externalTextures.size() ? externalTextures[idx] : nullptr; };
	};

	using ResourceName = std::string;
	enum class ResourceType { Texture };
	struct RenderGraphResource
	{
		ResourceName name;
		ResourceType type;
		std::variant<TextureDesc> desc;

		bool operator==(const RenderGraphResource& other) const { return name == other.name && type == other.type; }
		bool operator!=(const RenderGraphResource& other) const { return name != other.name || type != other.type; }
	};

	struct ExternalResource
	{
		ResourceName name;
		ResourceType type;

		bool operator==(const ExternalResource& other) const { return name == other.name && type == other.type; }
		bool operator!=(const ExternalResource& other) const { return name != other.name || type != other.type; }
	};
}

namespace std {
	template<> struct hash<OpenGLRenderGraph::TextureHandle> {
		size_t operator()(const OpenGLRenderGraph::TextureHandle& handle) const {
			return hash<uint32_t>()(handle.GetID());
		}
	};
}

namespace std {
	template<> struct hash<OpenGLRenderGraph::RenderGraphResource> {
		size_t operator()(const OpenGLRenderGraph::RenderGraphResource& res) const {
			size_t h1 = std::hash<std::string>{}(res.name);
			size_t h2 = std::hash<int>{}(static_cast<int>(res.type));
			return h1 ^ (h2 << 1);
		}
	};
}
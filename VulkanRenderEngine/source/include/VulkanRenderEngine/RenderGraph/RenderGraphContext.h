#pragma once

#include "vkstdafx.h"
#include "../Base/Texture2D.h"
#include "VulkanRenderEngine/VKWrapper/WrapperGeneral.h"


namespace RenderGraph
{

	struct TextureDesc {
		uint32_t width = 0;
		uint32_t height = 0;

		vk::Format format = vk::Format::eR8G8B8A8Unorm;
		vk::Filter minFilter = vk::Filter::eLinear;
		vk::Filter magFilter = vk::Filter::eLinear;
		vk::SamplerAddressMode wrapU = vk::SamplerAddressMode::eClampToEdge;
		vk::SamplerAddressMode wrapV = vk::SamplerAddressMode::eClampToEdge;

		uint32_t maxLevel = 1;
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

		std::vector<std::shared_ptr<Texture2D>> inputTextures;
		std::vector<std::shared_ptr<Texture2D>> optionInputTextures;
		std::vector<std::shared_ptr<Texture2D>> outputTextures;
		std::vector<std::shared_ptr<Texture2D>> tempTextures;
		std::vector<std::shared_ptr<Texture2D>> persitentTextures;
		std::vector<std::shared_ptr<Texture2D>> externalTextures;

		const std::string& GetName() const { return passName; };
		std::shared_ptr<Texture2D> GetInput(uint32_t idx) const { return idx >= 0 && idx < inputTextures.size() ? inputTextures[idx] : nullptr; };
		std::shared_ptr<Texture2D> GetOptionalInput(uint32_t idx) const { return idx >= 0 && idx < optionInputTextures.size() ? optionInputTextures[idx] : nullptr; };
		std::shared_ptr<Texture2D> GetOutput(uint32_t idx) const { return idx >= 0 && idx < outputTextures.size() ? outputTextures[idx] : nullptr; };
		std::shared_ptr<Texture2D> GetTemp(uint32_t idx) const { return idx >= 0 && idx < tempTextures.size() ? tempTextures[idx] : nullptr; };
		std::shared_ptr<Texture2D> GetPersitent(uint32_t idx) const { return idx >= 0 && idx < persitentTextures.size() ? persitentTextures[idx] : nullptr; };
		std::shared_ptr<Texture2D> GetExternal(uint32_t idx) const { return idx >= 0 && idx < externalTextures.size() ? externalTextures[idx] : nullptr; };
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


	class FrameDataRegistry
	{
	public:
		template<typename T>
		void Store(const std::string& name, T&& value) {
			data[name] = std::forward<T>(value);
		}

		template<typename T>
		void Store(const std::string& name, const T& value) {
			data[name] = value;
		}

		template<typename T>
		T Load(const std::string& name) const {
			auto it = data.find(name);
			if (it == data.end()) {
				throw std::runtime_error("Frame data not found: " + name);
			}
			return std::any_cast<T>(it->second);
		}

		template<typename T>
		T& Load(const std::string& name) {
			auto it = data.find(name);
			if (it == data.end()) {
				throw std::runtime_error("Frame data not found: " + name);
			}
			return std::any_cast<T&>(it->second);
		}

		template<typename T>
		T* TryLoad(const std::string& name) {
			auto it = data.find(name);
			if (it == data.end()) {
				return nullptr;
			}
			return std::any_cast<T>(&it->second);
		}

		// 检查是否存在
		bool Has(const std::string& name) const {
			return data.find(name) != data.end();
		}

		void Clear() {
			data.clear();
		}

	private:
		// 每帧一个注册表，存任意类型
		std::unordered_map<std::string, std::any> data;
	};

}

namespace std {
	template<> struct hash<RenderGraph::TextureHandle> {
		size_t operator()(const RenderGraph::TextureHandle& handle) const {
			return hash<uint32_t>()(handle.GetID());
		}
	};
}

namespace std {
	template<> struct hash<RenderGraph::RenderGraphResource> {
		size_t operator()(const RenderGraph::RenderGraphResource& res) const {
			size_t h1 = std::hash<std::string>{}(res.name);
			size_t h2 = std::hash<int>{}(static_cast<int>(res.type));
			return h1 ^ (h2 << 1);
		}
	};
}

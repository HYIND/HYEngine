#include "vkstdafx.h"
#include "RenderEngine/ImGuiTextureCache.h"

#include "VulkanRenderEngine/VKContext.h"
#include "VulkanRenderEngine/Base/Texture2D.h"
#include "imgui_impl_vulkan.h"

ImGuiTextureCache::ImGuiTextureCache(uint32_t releaseDelayFrames, uint32_t evictAfterFrames, bool autoTransition)
	: _releaseDelay(releaseDelayFrames), _evictAfter(std::max(1u, evictAfterFrames)), _autoTransition(autoTransition)
{}

ImGuiTextureCache::~ImGuiTextureCache()
{
	// 析构时后端是否还活着不确定（宿主应在关后端前显式 Clear），这里只丢记录
	_entries.clear();
	_pending.clear();
	_usedThisFrame.clear();
}

bool ImGuiTextureCache::WantsNearest(const Texture2D& texture)
{
	return texture.GetMagFilter() == vk::Filter::eNearest;
}

void ImGuiTextureCache::Release(VkDescriptorSet set)
{
	if (set == VK_NULL_HANDLE)
		return;
	_pending.emplace_back(set, _frame + _releaseDelay);
}

void ImGuiTextureCache::FlushPending(bool force)
{
	if (_pending.empty())
		return;

	LockGuard guard(VKCONTEXT->GetDescriptorPoolRequestMutex());
	while (!_pending.empty() && (force || _pending.front().second <= _frame))
	{
		ImGui_ImplVulkan_RemoveTexture(_pending.front().first);
		_pending.pop_front();
	}
}

void ImGuiTextureCache::BeginFrame()
{
	++_frame;
	_usedThisFrame.clear();

	// 很久没画的图：释放描述符集，也放掉对贴图的引用
	for (auto it = _entries.begin(); it != _entries.end();)
	{
		if (_frame - it->second.lastUsedFrame > _evictAfter)
		{
			Release(it->second.set);
			it = _entries.erase(it);
		}
		else
			++it;
	}

	FlushPending(false);
}

ImTextureID ImGuiTextureCache::Get(const std::shared_ptr<Texture2D>& texture)
{
	if (!texture || texture->IsEmpty())
		return ImTextureID_Invalid;

	if (_autoTransition && texture->GetLayout(0) != vk::ImageLayout::eShaderReadOnlyOptimal)
	{
		if (auto cmd = VKCONTEXT->GetCommandBuffer())
		{
			cmd->Begin();
			texture->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Read);
			cmd->End();
			cmd->SubmitNowAndWait();
		}
	}

	const uint32_t version = texture->GetDescBindEntryVersion();
	auto& entry = _entries[texture.get()];

	// 贴图重建过（Resize 等）：旧描述符集指向旧 image view，换一个
	if (entry.set != VK_NULL_HANDLE && entry.version != version)
	{
		Release(entry.set);
		entry.set = VK_NULL_HANDLE;
	}

	if (entry.set == VK_NULL_HANDLE)
	{
		LockGuard guard(VKCONTEXT->GetDescriptorPoolRequestMutex());
		entry.set = ImGui_ImplVulkan_AddTexture(
			static_cast<VkImageView>(texture->GetImageView(vk::ImageAspectFlagBits::eColor)),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		entry.texture = texture;
		entry.version = version;
		if (entry.set == VK_NULL_HANDLE)
		{
			_entries.erase(texture.get());
			return ImTextureID_Invalid;
		}
	}

	if (entry.lastUsedFrame != _frame)
	{
		entry.lastUsedFrame = _frame;
		_usedThisFrame.push_back(texture);
	}
	return reinterpret_cast<ImTextureID>(entry.set);
}

void ImGuiTextureCache::TransitionUsed(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd)
{
	if (!cmd)
		return;
	for (auto& texture : _usedThisFrame)
	{
		if (texture && !texture->IsEmpty())
			texture->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Read);
	}
}

void ImGuiTextureCache::Clear(bool backendAlive)
{
	if (backendAlive)
	{
		for (auto& [key, entry] : _entries)
			Release(entry.set);
		FlushPending(true);
	}
	_entries.clear();
	_pending.clear();
	_usedThisFrame.clear();
}

#pragma once

#include "vkstdafx.h"
#include "imgui.h"

#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

class Texture2D;
namespace VKWrapper { class VKCommandBuffer; }

// Texture2D -> ImTextureID 的缓存。ImGui 的 Vulkan 后端每张图要一个描述符集（ImGui_ImplVulkan_AddTexture），
// 描述符集属于"当前 ImGui 上下文的后端"，所以每个 ImGui 上下文各持有一份（RendererUI 一份、编辑器一份）。
//
// 用法（都在该上下文所在的线程上调用，且调用时这个上下文是 current）：
//   BeginFrame()          每帧开头：回收很久没用的条目、释放已过安全期的描述符集
//   Get(texture)          画图时取 ID；同一帧内同一张图只建一次
//   TransitionUsed(cmd)   （可选）提交前把这一帧用到的贴图切到可采样布局
//   Clear()               后端要关掉 / 重建之前调用（之后旧 ID 全部失效）
class ImGuiTextureCache
{
public:
	// releaseDelayFrames：RemoveTexture 推迟几帧（GPU 可能还在用上一帧的描述符集）。
	// 每帧都同步等 GPU 的（RendererUI）可以设 0；编辑器主窗口有帧并行，用默认值。
	// autoTransition：Get 时发现贴图不在可采样布局就立刻单独提交一次布局转换。
	// 宿主自己录 ImGui 命令、插不进 barrier 的（编辑器）打开；能调 TransitionUsed 的（RendererUI）关掉。
	explicit ImGuiTextureCache(uint32_t releaseDelayFrames = 3, uint32_t evictAfterFrames = 300, bool autoTransition = true);
	~ImGuiTextureCache();

	ImGuiTextureCache(const ImGuiTextureCache&) = delete;
	ImGuiTextureCache& operator=(const ImGuiTextureCache&) = delete;

	void BeginFrame();
	ImTextureID Get(const std::shared_ptr<Texture2D>& texture);
	// 立即不再缓存这张图（例如缩略图被淘汰）：描述符集照样推迟 releaseDelay 帧才真正释放，期间也继续持有贴图
	void Remove(const std::shared_ptr<Texture2D>& texture);
	void TransitionUsed(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd);

	// backendAlive = false：后端已经销毁（描述符池跟着没了），只丢掉记录，不再调用 RemoveTexture
	void Clear(bool backendAlive = true);

	size_t Size() const { return _entries.size(); }

	// 采样器：ImGui 后端默认线性；贴图配置成最近邻时在前后插入切换回调
	static bool WantsNearest(const Texture2D& texture);

private:
	struct Entry
	{
		std::shared_ptr<Texture2D> texture;		// 持有：描述符集引用着它的 image view
		VkDescriptorSet set = VK_NULL_HANDLE;
		uint32_t version = 0;
		uint64_t lastUsedFrame = 0;
	};

	// 推迟释放：GPU 可能还在用前几帧的描述符集，贴图也要跟着活到那时
	void Release(VkDescriptorSet set, std::shared_ptr<Texture2D> texture);
	void FlushPending(bool force);

private:
	std::unordered_map<const Texture2D*, Entry> _entries;
	struct Pending
	{
		VkDescriptorSet set = VK_NULL_HANDLE;
		uint64_t releaseFrame = 0;					// 到这一帧才可以释放
		std::shared_ptr<Texture2D> texture;			// 描述符集引用着它的 image view，一起活到释放
	};
	std::deque<Pending> _pending;
	std::vector<std::shared_ptr<Texture2D>> _usedThisFrame;
	uint64_t _frame = 0;
	uint32_t _releaseDelay = 3;
	uint32_t _evictAfter = 300;
	bool _autoTransition = true;
};

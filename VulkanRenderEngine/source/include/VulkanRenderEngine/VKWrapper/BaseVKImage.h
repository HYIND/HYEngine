#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/VKCore/CoreGeneral.h"
#include "VulkanRenderEngine/General/ImageLayoutWrapper.h"
#include "VKCommandBuffer.h"

#include <atomic>
#include <vector>

namespace VKWrapper {

	struct SubresourceState {
		vk::ImageLayout layout = vk::ImageLayout::eUndefined;
		vk::AccessFlags accessMask = vk::AccessFlagBits::eNone;
	};

	// 一张图每级 mip 的布局记录（各层共用一份）。
	// 线程安全：所有读写都在 lock 里完成，元素的引用不会带出锁外；mips 在建图时按 mip 数分配好。
	struct ImageState
	{
		mutable SpinLock lock;
		std::vector<SubresourceState> mips;
	};

	class BaseVKImage;

	// RenderGraph 一个 Pass 录制期间的布局记录（线程局部）：Pass 里的布局切换先记在这里，
	// 第一次提交时（EndRecord）对比真实记录补屏障，再写回真实记录。
	// 加锁顺序固定为：提交锁（GraphSubmitContext）→ 本记录的 _mutex → 图的 ImageState::lock，不会反过来拿。
	class PassImageStateRecord
	{
	public:
		static void StartRecord();
		static void AddState(std::shared_ptr<const BaseVKImage> image, ImageLayout::BindStage stage, ImageLayout::BindUsage usage);
		static void EndRecord(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd);
		static std::shared_ptr<PassImageStateRecord> Current();

	private:
		struct Entry
		{
			std::vector<SubresourceState> first;	// Pass 声明的 / 第一次碰到时的状态：EndRecord 从真实态补差到这里
			std::vector<SubresourceState> current;	// Pass 里录到哪了：EndRecord 写回真实态
		};

		// 调用方持有 _mutex：取这张图在本 Pass 里的记录，第一次碰到时从真实记录拷一份
		Entry& GetEntryLocked(const std::shared_ptr<const BaseVKImage>& image);

	private:
		std::unordered_map<std::shared_ptr<const BaseVKImage>, Entry> _entries;
		SpinLock _mutex;

		friend class BaseVKImage;
	};

	class BaseVKImage :public std::enable_shared_from_this<BaseVKImage>
	{
	public:
		BaseVKImage() = default;
		virtual ~BaseVKImage() = default;

		BaseVKImage(const BaseVKImage&) = delete;
		BaseVKImage& operator=(const BaseVKImage&) = delete;

		vk::Image GetHandle() const;
		vk::ImageLayout GetCurrentLayout(uint32_t level = 0) const;
		uint32_t GetMipLevels() const;
		vk::Extent3D GetExtent() const;
		vk::Format GetFormat() const;
		operator vk::Image() const;

		// 布局切换
		void TransitionLayout(
			std::shared_ptr<VKCommandBuffer> cmd,
			vk::ImageLayout newLayout,
			vk::PipelineStageFlags dstStageMask,
			uint32_t baseMipLevel = 0,
			uint32_t levelCount = vk::RemainingMipLevels,
			bool force = false
		);

		// 采样读（combined image sampler）用的布局切换，返回描述符里要写的布局。
		// 目标布局取 GetImageLayoutAndStageFlag(..., BindUsage::Sample)；Compute / RayTracing 下如果范围内各级 mip
		// 已经全部是 GENERAL 就保持 GENERAL（GENERAL 也可以采样），这样 Graph 里预设为 computeReadLayout(GENERAL)
		// 的输入不会在 Pass 里被切到只读、下一个 Pass 又切回来。
		vk::ImageLayout TransitionLayoutForSample(
			std::shared_ptr<VKCommandBuffer> cmd,
			ImageLayout::BindStage stage,
			uint32_t baseMipLevel = 0,
			uint32_t levelCount = vk::RemainingMipLevels
		);

		bool UploadData(
			const void* data,
			uint32_t mipLevel = 0,
			uint32_t layer = 0
		);

		virtual void Release();

		void GenerateMipMaps();

		// ---- 只读资产贴图 ----
		// 把所有 mip 切到 SHADER_READ_ONLY_OPTIMAL 并等 GPU 做完，之后这张图的布局永远不变：
		//   * 采样用途的布局切换直接返回只读布局：不读写布局记录、不录屏障，Pass 的布局记录也跳过它；
		//   * 写入、传输、生成 mip、上传数据都会被拒绝（打一次错误日志）。
		// 只用于加载一次、之后只采样的图（Texture / Sprite / 动图图集页等资产贴图）：
		// 这类图会被 2D、3D、UI、编辑器在不同线程、不同帧同时采样，布局固定后谁先谁后都不会出错。
		// 只支持颜色格式；要改内容就新建一张图替换。
		bool MakeSampledImmutable();
		bool IsSampledImmutable() const { return m_sampledImmutable.load(std::memory_order_acquire); }

	protected:
		// 建图后调用（m_mipLevels 已设好）：按 mip 数重建布局记录，清掉只读标记
		void ResetImageState(vk::ImageLayout initialLayout);

	private:
		// 在锁里访问这张图当前生效的布局记录：在 Pass 录制期间是本 Pass 的记录，否则是真实记录
		template<typename F>
		void VisitStates(F&& f) const;

		// 调用方已持有 states 所属的锁：对 [baseMipLevel, endMipLevel) 逐级补屏障并更新记录
		void TransitionStatesLocked(std::vector<SubresourceState>& states,
			const std::shared_ptr<VKCommandBuffer>& cmd, vk::ImageLayout newLayout, vk::PipelineStageFlags dstStageMask,
			uint32_t baseMipLevel, uint32_t endMipLevel, bool force) const;

		void ReportImmutableMisuse(const char* what) const;

	protected:
		VKCore::VulkanDevice* m_device = nullptr;
		vk::Image m_image = VK_NULL_HANDLE;

		uint32_t m_layerCount = 1;
		uint32_t m_mipLevels = 1;
		vk::Extent3D m_extent = vk::Extent3D();
		vk::Format m_format = vk::Format::eUndefined;

		mutable ImageState m_imageState;
		std::atomic<bool> m_sampledImmutable{ false };
		mutable std::atomic<bool> m_misuseReported{ false };

		friend class PassImageStateRecord;
	};

} // namespace VKWrapper

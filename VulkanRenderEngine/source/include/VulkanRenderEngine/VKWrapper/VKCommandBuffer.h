#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine\VKCore\VulkanDevice.h"
#include "VulkanRenderEngine\VKCore\VulkanOutput.h"
#include "VKCommandPool.h"
#include "VKRenderPass.h"

#define VK_CMD_WRAPPER(CMD_NAME) \
    template <typename... Args> \
    auto CMD_NAME(Args&&... args) -> decltype(auto) { \
        return _handle.CMD_NAME(std::forward<Args>(args)...); \
    }


#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

class DynamicBlock;
class VertexBufferBlock;
class IndexBufferBlock;
class IndirectBufferBlock;

struct DynamicViewport
{
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t x = 0;
	uint32_t y = 0;

	DynamicViewport() {}
	DynamicViewport(uint32_t width, uint32_t height)
		:width(width), height(height), x(0u), y(0u)
	{}
	DynamicViewport(uint32_t width, uint32_t height, uint32_t x, uint32_t y)
		:width(width), height(height), x(x), y(y)
	{}
};

struct DynamicRenderInfo
{

	std::vector<vk::RenderingAttachmentInfo> colorAttachments;
	vk::RenderingAttachmentInfo* depthStencilAttachment = nullptr;
	vk::RenderingAttachmentInfo* depthAttachment = nullptr;
	vk::RenderingAttachmentInfo* stencilAttachment = nullptr;
	vk::RenderingInfo renderingInfo;

	DynamicRenderInfo() = default;
	~DynamicRenderInfo() {
		if (depthStencilAttachment)
			delete depthStencilAttachment;
		if (depthAttachment)
			delete depthAttachment;
		if (stencilAttachment)
			delete stencilAttachment;
	}

	DynamicRenderInfo& SetRenderArea(uint32_t width, uint32_t height, uint32_t x = 0, uint32_t y = 0);
	DynamicRenderInfo& AddColorAttachment(vk::ImageView imageView, vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eClear, vk::ClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 0.0f });
	DynamicRenderInfo& AddDepthAttachment(vk::ImageView imageView, vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eClear, vk::ClearDepthStencilValue clearValue = { 1.0f, 0u });
	DynamicRenderInfo& AddStencilAttachment(vk::ImageView imageView, vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eClear, vk::ClearDepthStencilValue clearValue = { 1.0f, 0u });
	DynamicRenderInfo& AddDepthStencilAttachment(vk::ImageView imageView, vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eClear, vk::ClearDepthStencilValue clearValue = { 1.0f, 0u });

};

namespace VKWrapper
{

	class VKCommandBuffer
	{
	public:
		VKCommandBuffer() = default;
		VKCommandBuffer(VKCommandPool* pool);

		~VKCommandBuffer();

		void Create(VKCommandPool* pool);
		void Release();

		vk::CommandBuffer GetHandle() const;
		bool IsRecording() const;

		vk::Result Begin(vk::CommandBufferUsageFlags usageFlags, vk::CommandBufferInheritanceInfo& inheritanceInfo) const;
		vk::Result Begin(vk::CommandBufferUsageFlags usageFlags = {}) const;
		vk::Result End() const;
		vk::Result Reset() const;

	private:
		void Need() const;

	private:
		VKCommandPool* _pool = nullptr;
		vk::CommandBuffer _handle = VK_NULL_HANDLE;
		mutable bool _isRecording = false;

		friend class VKCommandPool;

	public:

		// 绑定命令
		void bindPipeline(vk::PipelineBindPoint pipelineBindPoint, vk::Pipeline pipeline);
		void bindVertexBuffers(uint32_t firstBinding, uint32_t bindingCount, const vk::Buffer* pBuffers, const vk::DeviceSize* pOffsets);
		void bindVertexBuffers(uint32_t firstBinding, vk::ArrayProxy<vk::Buffer const> const& buffers, vk::ArrayProxy<vk::DeviceSize const> const& offsets);
		void bindIndexBuffer(vk::Buffer buffer, vk::DeviceSize offset, vk::IndexType indexType);
		void bindDescriptorSets(vk::PipelineBindPoint pipelineBindPoint, vk::PipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const vk::DescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets);
		void bindDescriptorSets(vk::PipelineBindPoint pipelineBindPoint, vk::PipelineLayout layout, uint32_t firstSet, vk::ArrayProxy<vk::DescriptorSet const> const& descriptorSets, vk::ArrayProxy<uint32_t const> const& dynamicOffsets);

		void bindVertexBuffers(const std::shared_ptr<VertexBufferBlock>& vertexBufferBlock, uint32_t firstBinding = 0, const vk::DeviceSize& offset = 0);				//bindVertexBuffers封装
		void bindIndexBuffer(const std::shared_ptr<IndexBufferBlock>& indexBufferBlock, vk::DeviceSize offset = 0, vk::IndexType indexType = vk::IndexType::eUint32);	//bindIndexBuffer封装


		// 绘制命令
		void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
		void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
		void drawIndirect(vk::Buffer buffer, vk::DeviceSize offset, uint32_t drawCount, uint32_t stride);
		void drawIndexedIndirect(vk::Buffer buffer, uint32_t drawCount, uint32_t stride = sizeof(IndirectDrawCommand), vk::DeviceSize offset = 0ULL);
		void drawIndexedIndirect(const std::shared_ptr<IndirectBufferBlock>& block, uint32_t drawCount, uint32_t stride = sizeof(IndirectDrawCommand), vk::DeviceSize offset = 0ULL);

		// 计算命令
		void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ = 1);
		void dispatchIndirect(vk::Buffer buffer, vk::DeviceSize offset);

		// 拷贝/传输命令
		void copyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, uint32_t regionCount, const vk::BufferCopy* pRegions);
		void copyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, const vk::ArrayProxy<const vk::BufferCopy>& regions);
		void copyImage(vk::Image srcImage, vk::ImageLayout srcImageLayout, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::ArrayProxy<vk::ImageCopy const> const& regions);
		void blitImage(vk::Image srcImage, vk::ImageLayout srcImageLayout, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::ArrayProxy<vk::ImageBlit const> const& regions, vk::Filter filter);
		void resolveImage(vk::Image srcImage, vk::ImageLayout srcImageLayout, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::ArrayProxy<vk::ImageResolve const> const& regions);
		void updateBuffer(vk::Buffer dstBuffer, vk::DeviceSize dstOffset, vk::DeviceSize dataSize, const void* pData);
		void fillBuffer(vk::Buffer dstBuffer, vk::DeviceSize dstOffset, vk::DeviceSize size, uint32_t data);
		void copyBufferToImage(vk::Buffer srcBuffer, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::ArrayProxy<vk::BufferImageCopy const> const& regions);

		// 清除命令
		void clearColorImage(vk::Image image, vk::ImageLayout imageLayout, const vk::ClearColorValue& color, vk::ArrayProxy<vk::ImageSubresourceRange const> const& ranges);
		void clearDepthStencilImage(vk::Image image, vk::ImageLayout imageLayout, const vk::ClearDepthStencilValue& depthStencil, vk::ArrayProxy<vk::ImageSubresourceRange const> const& ranges);

		// 同步命令
		void pipelineBarrier(vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask, vk::DependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const vk::MemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const vk::BufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const vk::ImageMemoryBarrier* pImageMemoryBarriers);
		void pipelineBarrier(vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask, vk::DependencyFlags dependencyFlags, vk::ArrayProxy<vk::MemoryBarrier const> const& memoryBarriers, vk::ArrayProxy<vk::BufferMemoryBarrier const> const& bufferMemoryBarriers, vk::ArrayProxy<vk::ImageMemoryBarrier const> const& imageMemoryBarriers);

		// 视口/裁剪命令
		void setViewportWithCount(vk::ArrayProxy<vk::Viewport const> const& viewports);
		void setScissorWithCount(vk::ArrayProxy<vk::Rect2D const> const& scissors);
		void setDynamicViewports(vk::ArrayProxy<DynamicViewport const> const& viewports);			//封装setViewportWithCount和setScissorWithCount
		void setDynamicViewport(uint32_t width, uint32_t height, uint32_t x = 0, uint32_t y = 0);	//封装setViewportWithCount和setScissorWithCount
		void setDynamicViewport(const DynamicViewport& viewPort);


		void setLineWidth(float lineWidth) { _handle.setLineWidth(lineWidth); }
		void setDepthBias(float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor);
		void setBlendConstants(const float blendConstants[4]);
		void setStencilCompareMask(vk::StencilFaceFlags faceMask, uint32_t compareMask);
		void setStencilWriteMask(vk::StencilFaceFlags faceMask, uint32_t writeMask);
		void setStencilReference(vk::StencilFaceFlags faceMask, uint32_t reference);

		// 查询命令
		void resetQueryPool(vk::QueryPool queryPool, uint32_t firstQuery, uint32_t queryCount);
		void beginQuery(vk::QueryPool queryPool, uint32_t query, vk::QueryControlFlags flags);
		void endQuery(vk::QueryPool queryPool, uint32_t query);
		void writeTimestamp(vk::PipelineStageFlagBits pipelineStage, vk::QueryPool queryPool, uint32_t query);
		void copyQueryPoolResults(vk::QueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, vk::Buffer dstBuffer, vk::DeviceSize dstOffset, vk::DeviceSize stride, vk::QueryResultFlags flags);

		// 推送常量
		void pushConstants(vk::PipelineLayout layout, vk::ShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues);

		template <typename ValuesType>
		void pushConstants(vk::PipelineLayout layout, vk::ShaderStageFlags stageFlags, uint32_t offset, vk::ArrayProxy<ValuesType const> const& values) {
			_handle.pushConstants(layout, stageFlags, offset, values);
		}

		// RenderPass
		void beginRenderPass(const vk::RenderPassBeginInfo& beginInfo, vk::SubpassContents subpassContents);
		void nextSubpass(vk::SubpassContents subpassContents);
		void endRenderPass();

		// Attachment
		void clearAttachments(vk::ArrayProxy<vk::ClearAttachment const> const& attachments, vk::ArrayProxy<vk::ClearRect const> const& rects);

		// 动态渲染
		void beginRendering(vk::RenderingInfo const& renderingInfo);
		void beginRendering(const DynamicRenderInfo& dynamicRenderInfo);	//封装
		void endRendering();

		// 动态剔除
		void setCullMode(vk::CullModeFlags cullMode);

		// 光追管线
		void buildAccelerationStructuresKHR(vk::ArrayProxy<vk::AccelerationStructureBuildGeometryInfoKHR const> const& infos, vk::ArrayProxy<vk::AccelerationStructureBuildRangeInfoKHR const* const> const& pBuildRangeInfos);
		void traceRaysKHR(vk::StridedDeviceAddressRegionKHR const& raygenShaderBindingTable, vk::StridedDeviceAddressRegionKHR const& missShaderBindingTable, vk::StridedDeviceAddressRegionKHR const& hitShaderBindingTable, vk::StridedDeviceAddressRegionKHR const& callableShaderBindingTable, uint32_t width, uint32_t height, uint32_t depth);
	};
}
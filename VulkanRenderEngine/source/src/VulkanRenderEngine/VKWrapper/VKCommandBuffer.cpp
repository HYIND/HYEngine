#include "vkstdafx.h"
#include "VulkanRenderEngine/VKWrapper/VKCommandBuffer.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"

#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

using namespace VKWrapper;

class RestireCommandBuffer :public IVKResource
{
public:
	RestireCommandBuffer(std::weak_ptr<VKCommandPool>&& pool, vk::CommandBuffer handle)
		:_pool(pool), _handle(handle)
	{}
	virtual void Destroy() {
		if (auto pool = _pool.lock())
			pool->FreeBuffers(_handle);
	}

public:
	std::weak_ptr<VKCommandPool> _pool;
	vk::CommandBuffer _handle = VK_NULL_HANDLE;
};

VKCommandBuffer::VKCommandBuffer(VKCommandPool* pool)
{}

VKWrapper::VKCommandBuffer::~VKCommandBuffer()
{
	Release();
}

void VKCommandBuffer::Release()
{
	if (auto pool = _pool.lock(); pool && _handle)
	{
		if (IsRecording())
			Reset();
		VKCONTEXT->Retire(new RestireCommandBuffer(std::move(_pool), _handle));
	}

	_pool.reset();
	_handle = VK_NULL_HANDLE;
}

vk::CommandBuffer VKCommandBuffer::GetHandle() const { return _handle; }

bool VKWrapper::VKCommandBuffer::IsRecording() const { return _isRecording; }

void VKWrapper::VKCommandBuffer::Need() const {
	if (!_isRecording)
		Begin();
}

vk::Result VKCommandBuffer::Begin(vk::CommandBufferUsageFlags usageFlags, vk::CommandBufferInheritanceInfo& inheritanceInfo) const
{
	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.setFlags(usageFlags)
		.setPInheritanceInfo(&inheritanceInfo);

	if (auto result = _handle.begin(beginInfo); result != vk::Result::eSuccess)
	{
		outStream << std::format("[ commandBuffer ] ERROR\nFailed to begin a command buffer!\nError code: {}\n", to_string(result));
		return result;
	}
	_isRecording = true;
	return vk::Result::eSuccess;
}

vk::Result VKCommandBuffer::Begin(vk::CommandBufferUsageFlags usageFlags) const
{
	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.setFlags(usageFlags);

	if (auto result = _handle.begin(beginInfo); result != vk::Result::eSuccess)
	{
		outStream << std::format("[ commandBuffer ] ERROR\nFailed to begin a command buffer!\nError code: {}\n", to_string(result));
		return result;
	}
	_isRecording = true;
	return vk::Result::eSuccess;
}

vk::Result VKCommandBuffer::End() const
{
	_isRecording = false;
	if (auto result = _handle.end(); result != vk::Result::eSuccess)
	{
		outStream << std::format("[ commandBuffer ] ERROR\nFailed to end a command buffer!\nError code: {}\n", to_string(result));
		return result;
	}
	return vk::Result::eSuccess;
}

vk::Result VKCommandBuffer::Reset() const
{
	_isRecording = false;
	if (auto result = _handle.reset(); result != vk::Result::eSuccess)
	{
		outStream << std::format("[ commandBuffer ] ERROR\nFailed to reset a command buffer!\nError code: {}\n", to_string(result));
		return result;
	}
	return vk::Result::eSuccess;
}

// 绑定命令
void VKWrapper::VKCommandBuffer::bindPipeline(vk::PipelineBindPoint pipelineBindPoint, vk::Pipeline pipeline) {
	Need();
	_handle.bindPipeline(pipelineBindPoint, pipeline);
}

void VKWrapper::VKCommandBuffer::bindVertexBuffers(uint32_t firstBinding, uint32_t bindingCount, const vk::Buffer* pBuffers, const vk::DeviceSize* pOffsets) {
	Need();
	_handle.bindVertexBuffers(firstBinding, bindingCount, pBuffers, pOffsets);
}

void VKWrapper::VKCommandBuffer::bindVertexBuffers(uint32_t firstBinding, vk::ArrayProxy<vk::Buffer const> const& buffers, vk::ArrayProxy<vk::DeviceSize const> const& offsets) {
	Need();
	_handle.bindVertexBuffers(firstBinding, buffers, offsets);
}

void VKWrapper::VKCommandBuffer::bindIndexBuffer(vk::Buffer buffer, vk::DeviceSize offset, vk::IndexType indexType) {
	Need();
	_handle.bindIndexBuffer(buffer, offset, indexType);
}

void VKWrapper::VKCommandBuffer::bindDescriptorSets(vk::PipelineBindPoint pipelineBindPoint, vk::PipelineLayout layout, uint32_t firstSet, vk::ArrayProxy<vk::DescriptorSet const> const& descriptorSets, vk::ArrayProxy<uint32_t const> const& dynamicOffsets) {
	Need();
	_handle.bindDescriptorSets(pipelineBindPoint, layout, firstSet, descriptorSets, dynamicOffsets);
}

void VKWrapper::VKCommandBuffer::bindVertexBuffers(const std::shared_ptr<VertexBufferBlock>& vertexBufferBlock, uint32_t firstBinding, const vk::DeviceSize& offset)
{
	bindVertexBuffers(firstBinding, vertexBufferBlock->GetBuffer()->GetHandle(), offset);
}

void VKWrapper::VKCommandBuffer::bindIndexBuffer(const std::shared_ptr<IndexBufferBlock>& indexBufferBlock, vk::DeviceSize offset, vk::IndexType indexType)
{
	bindIndexBuffer(indexBufferBlock->GetBuffer()->GetHandle(), offset, indexType);
}

// 绘制命令
void VKWrapper::VKCommandBuffer::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
	Need();
	_handle.draw(vertexCount, instanceCount, firstVertex, firstInstance);
}

void VKWrapper::VKCommandBuffer::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
	Need();
	_handle.drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VKWrapper::VKCommandBuffer::drawIndirect(vk::Buffer buffer, vk::DeviceSize offset, uint32_t drawCount, uint32_t stride) {
	Need();
	_handle.drawIndirect(buffer, offset, drawCount, stride);
}

void VKWrapper::VKCommandBuffer::drawIndexedIndirect(vk::Buffer buffer, uint32_t drawCount, uint32_t stride, vk::DeviceSize offset) {
	Need();
	_handle.drawIndexedIndirect(buffer, offset, drawCount, stride);
}

void VKWrapper::VKCommandBuffer::drawIndexedIndirect(const std::shared_ptr<IndirectBufferBlock>& block, uint32_t drawCount, uint32_t stride, vk::DeviceSize offset) {
	Need();
	_handle.drawIndexedIndirect(block->GetBuffer()->GetHandle(), offset, drawCount, stride);
}

// 计算命令
void VKWrapper::VKCommandBuffer::dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
	Need();
	_handle.dispatch(groupCountX, groupCountY, groupCountZ);
}

void VKWrapper::VKCommandBuffer::dispatchIndirect(vk::Buffer buffer, vk::DeviceSize offset) {
	Need();
	_handle.dispatchIndirect(buffer, offset);
}

// 拷贝/传输命令
void VKWrapper::VKCommandBuffer::copyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, uint32_t regionCount, const vk::BufferCopy* pRegions) {
	Need();
	_handle.copyBuffer(srcBuffer, dstBuffer, regionCount, pRegions);
}

void VKWrapper::VKCommandBuffer::copyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, const vk::ArrayProxy<const vk::BufferCopy>& regions) {
	Need();
	_handle.copyBuffer(srcBuffer, dstBuffer, regions);
}

void VKWrapper::VKCommandBuffer::copyImage(vk::Image srcImage, vk::ImageLayout srcImageLayout, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::ArrayProxy<vk::ImageCopy const> const& regions) {
	Need();
	_handle.copyImage(srcImage, srcImageLayout, dstImage, dstImageLayout, regions);
}

void VKWrapper::VKCommandBuffer::blitImage(vk::Image srcImage, vk::ImageLayout srcImageLayout, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::ArrayProxy<vk::ImageBlit const> const& regions, vk::Filter filter) {
	Need();
	_handle.blitImage(srcImage, srcImageLayout, dstImage, dstImageLayout, regions, filter);
}

void VKWrapper::VKCommandBuffer::resolveImage(vk::Image srcImage, vk::ImageLayout srcImageLayout, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::ArrayProxy<vk::ImageResolve const> const& regions) {
	Need();
	_handle.resolveImage(srcImage, srcImageLayout, dstImage, dstImageLayout, regions);
}

void VKWrapper::VKCommandBuffer::updateBuffer(vk::Buffer dstBuffer, vk::DeviceSize dstOffset, vk::DeviceSize dataSize, const void* pData) {
	Need();
	_handle.updateBuffer(dstBuffer, dstOffset, dataSize, pData);
}

void VKWrapper::VKCommandBuffer::fillBuffer(vk::Buffer dstBuffer, vk::DeviceSize dstOffset, vk::DeviceSize size, uint32_t data) {
	Need();
	_handle.fillBuffer(dstBuffer, dstOffset, size, data);
}

void VKWrapper::VKCommandBuffer::copyBufferToImage(vk::Buffer srcBuffer, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::ArrayProxy<vk::BufferImageCopy const> const& regions) {
	Need();
	return _handle.copyBufferToImage(srcBuffer, dstImage, dstImageLayout, regions);
}

// 清除命令
void VKWrapper::VKCommandBuffer::clearColorImage(vk::Image image, vk::ImageLayout imageLayout, const vk::ClearColorValue& color, vk::ArrayProxy<vk::ImageSubresourceRange const> const& ranges) {
	Need();
	_handle.clearColorImage(image, imageLayout, color, ranges);
}

void VKWrapper::VKCommandBuffer::clearDepthStencilImage(vk::Image image, vk::ImageLayout imageLayout, const vk::ClearDepthStencilValue& depthStencil, vk::ArrayProxy<vk::ImageSubresourceRange const> const& ranges) {
	Need();
	_handle.clearDepthStencilImage(image, imageLayout, depthStencil, ranges);
}

// 同步命令
void VKWrapper::VKCommandBuffer::pipelineBarrier(vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask, vk::ArrayProxy<vk::MemoryBarrier const> const& memoryBarriers, vk::ArrayProxy<vk::BufferMemoryBarrier const> const& bufferMemoryBarriers, vk::ArrayProxy<vk::ImageMemoryBarrier const> const& imageMemoryBarriers, vk::DependencyFlags dependencyFlags) {
	Need();
	_handle.pipelineBarrier(srcStageMask, dstStageMask, dependencyFlags, memoryBarriers, bufferMemoryBarriers, imageMemoryBarriers);
}

void VKWrapper::VKCommandBuffer::pipelineBarrier(vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask, vk::ArrayProxy<vk::MemoryBarrier const> const& memoryBarriers, vk::DependencyFlags dependencyFlags)
{
	Need();
	_handle.pipelineBarrier(srcStageMask, dstStageMask, dependencyFlags, memoryBarriers, {}, {});
}

void VKWrapper::VKCommandBuffer::pipelineBarrier(vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask, vk::ArrayProxy<vk::BufferMemoryBarrier const> const& bufferMemoryBarriers, vk::DependencyFlags dependencyFlags)
{
	Need();
	_handle.pipelineBarrier(srcStageMask, dstStageMask, dependencyFlags, {}, bufferMemoryBarriers, {});
}

void VKWrapper::VKCommandBuffer::pipelineBarrier(vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask, vk::ArrayProxy<vk::ImageMemoryBarrier const> const& imageMemoryBarriers, vk::DependencyFlags dependencyFlags)
{
	Need();
	_handle.pipelineBarrier(srcStageMask, dstStageMask, dependencyFlags, {}, {}, imageMemoryBarriers);
}

void VKWrapper::VKCommandBuffer::pipelineBarrier2(vk::ArrayProxyNoTemporaries<vk::MemoryBarrier2 const> const& memoryBarriers, vk::ArrayProxyNoTemporaries<vk::BufferMemoryBarrier2 const> const& bufferMemoryBarriers, vk::ArrayProxyNoTemporaries<vk::ImageMemoryBarrier2 const> const& imageMemoryBarriers, vk::DependencyFlags dependencyFlags)
{
	Need();
	vk::DependencyInfo dep;
	dep
		.setDependencyFlags(dependencyFlags)
		.setMemoryBarriers(memoryBarriers)
		.setBufferMemoryBarriers(bufferMemoryBarriers)
		.setImageMemoryBarriers(imageMemoryBarriers);
	_handle.pipelineBarrier2(dep);
}

void VKWrapper::VKCommandBuffer::pipelineBarrier2(vk::ArrayProxyNoTemporaries<vk::MemoryBarrier2 const> const& memoryBarriers, vk::DependencyFlags dependencyFlags)
{
	Need();
	vk::DependencyInfo dep;
	dep
		.setDependencyFlags(dependencyFlags)
		.setMemoryBarriers(memoryBarriers);
	_handle.pipelineBarrier2(dep);
}
void VKWrapper::VKCommandBuffer::pipelineBarrier2(vk::ArrayProxyNoTemporaries<vk::BufferMemoryBarrier2 const> const& bufferMemoryBarriers, vk::DependencyFlags dependencyFlags)
{
	Need();
	vk::DependencyInfo dep;
	dep
		.setDependencyFlags(dependencyFlags)
		.setBufferMemoryBarriers(bufferMemoryBarriers);
	_handle.pipelineBarrier2(dep);
}
void VKWrapper::VKCommandBuffer::pipelineBarrier2(vk::ArrayProxyNoTemporaries<vk::ImageMemoryBarrier2 const> const& imageMemoryBarriers, vk::DependencyFlags dependencyFlags)
{
	Need();
	vk::DependencyInfo dep;
	dep
		.setDependencyFlags(dependencyFlags)
		.setImageMemoryBarriers(imageMemoryBarriers);
	_handle.pipelineBarrier2(dep);
}

// 视口/裁剪命令
void VKWrapper::VKCommandBuffer::setViewportWithCount(vk::ArrayProxy<vk::Viewport const> const& viewports) {
	Need();
	_handle.setViewportWithCount(viewports);
}

void VKWrapper::VKCommandBuffer::setScissorWithCount(vk::ArrayProxy<vk::Rect2D const> const& scissors) {
	Need();
	_handle.setScissorWithCount(scissors);
}

void VKWrapper::VKCommandBuffer::setDynamicViewports(vk::ArrayProxy<DynamicViewport const> const& viewports) {
	Need();
	std::vector<vk::Viewport> tempviewports; tempviewports.reserve(viewports.size());
	std::vector<vk::Rect2D> tempscissors; tempscissors.reserve(viewports.size());
	for (auto& viewport : viewports)
	{
		tempviewports.push_back({ float(viewport.x), float(viewport.y), float(viewport.width), float(viewport.height) ,0.f, 1.f });
		tempscissors.push_back({ {int32_t(viewport.x), int32_t(viewport.y) },{uint32_t(viewport.width) ,uint32_t(viewport.height)} });
	}
	_handle.setViewportWithCount(tempviewports);
	_handle.setScissorWithCount(tempscissors);
}

void VKWrapper::VKCommandBuffer::setDynamicViewport(uint32_t width, uint32_t height, uint32_t x, uint32_t y) {
	Need();
	vk::Viewport tempviewport{ float(x), float(y), float(width), float(height) ,0.f, 1.f };
	vk::Rect2D tempscissor{ { int32_t(0), int32_t(0) }, { uint32_t(width) ,uint32_t(height) } };
	_handle.setViewportWithCount(tempviewport);
	_handle.setScissorWithCount(tempscissor);
}

void VKWrapper::VKCommandBuffer::setDynamicViewport(const DynamicViewport& viewPort) {
	Need();
	vk::Viewport tempviewport{ float(viewPort.x), float(viewPort.y), float(viewPort.width), float(viewPort.height) ,0.f, 1.f };
	vk::Rect2D tempscissor{ { int32_t(0), int32_t(0) }, { uint32_t(viewPort.width) ,uint32_t(viewPort.height) } };
	_handle.setViewportWithCount(tempviewport);
	_handle.setScissorWithCount(tempscissor);
}

void VKWrapper::VKCommandBuffer::setDepthBias(float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor) {
	Need();
	_handle.setDepthBias(depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor);
}

void VKWrapper::VKCommandBuffer::setBlendConstants(const float blendConstants[4]) {
	Need();
	_handle.setBlendConstants(blendConstants);
}

void VKWrapper::VKCommandBuffer::setStencilCompareMask(vk::StencilFaceFlags faceMask, uint32_t compareMask) {
	Need();
	_handle.setStencilCompareMask(faceMask, compareMask);
}

void VKWrapper::VKCommandBuffer::setStencilWriteMask(vk::StencilFaceFlags faceMask, uint32_t writeMask) {
	Need();
	_handle.setStencilWriteMask(faceMask, writeMask);
}

void VKWrapper::VKCommandBuffer::setStencilReference(vk::StencilFaceFlags faceMask, uint32_t reference) {
	Need();
	_handle.setStencilReference(faceMask, reference);
}

// 查询命令
void VKWrapper::VKCommandBuffer::resetQueryPool(vk::QueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) {
	Need();
	_handle.resetQueryPool(queryPool, firstQuery, queryCount);
}

void VKWrapper::VKCommandBuffer::beginQuery(vk::QueryPool queryPool, uint32_t query, vk::QueryControlFlags flags) {
	Need();
	_handle.beginQuery(queryPool, query, flags);
}

void VKWrapper::VKCommandBuffer::endQuery(vk::QueryPool queryPool, uint32_t query) {
	Need();
	_handle.endQuery(queryPool, query);
}

void VKWrapper::VKCommandBuffer::writeTimestamp(vk::PipelineStageFlagBits pipelineStage, vk::QueryPool queryPool, uint32_t query) {
	Need();
	_handle.writeTimestamp(pipelineStage, queryPool, query);
}

void VKWrapper::VKCommandBuffer::copyQueryPoolResults(vk::QueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, vk::Buffer dstBuffer, vk::DeviceSize dstOffset, vk::DeviceSize stride, vk::QueryResultFlags flags) {
	Need();
	_handle.copyQueryPoolResults(queryPool, firstQuery, queryCount, dstBuffer, dstOffset, stride, flags);
}

// 推送常量
void VKWrapper::VKCommandBuffer::pushConstants(vk::PipelineLayout layout, vk::ShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues) {
	Need();
	_handle.pushConstants(layout, stageFlags, offset, size, pValues);
}

// RenderPass
void VKWrapper::VKCommandBuffer::beginRenderPass(const vk::RenderPassBeginInfo& beginInfo, vk::SubpassContents subpassContents) {
	Need();
	_handle.beginRenderPass(beginInfo, subpassContents);
}

void VKWrapper::VKCommandBuffer::nextSubpass(vk::SubpassContents subpassContents) {
	Need();
	_handle.nextSubpass(subpassContents);
}

void VKWrapper::VKCommandBuffer::endRenderPass() {
	Need();
	_handle.endRenderPass();
}

void VKWrapper::VKCommandBuffer::clearAttachments(vk::ArrayProxy<vk::ClearAttachment const> const& attachments, vk::ArrayProxy<vk::ClearRect const> const& rects) {
	Need();
	_handle.clearAttachments(attachments, rects);
}

void VKWrapper::VKCommandBuffer::beginRendering(vk::RenderingInfo const& renderingInfo) {
	Need();
	_handle.beginRendering(renderingInfo);
}

void VKWrapper::VKCommandBuffer::beginRendering(const DynamicRenderInfo& dynamicRenderInfo) {
	Need();
	_handle.beginRendering(dynamicRenderInfo.renderingInfo);
}

void VKWrapper::VKCommandBuffer::endRendering() {
	Need();
	_handle.endRendering();
}

// 动态剔除
void VKWrapper::VKCommandBuffer::setCullMode(vk::CullModeFlags cullMode) {
	Need();
	_handle.setCullMode(cullMode);
}

// 光追管线
void VKWrapper::VKCommandBuffer::buildAccelerationStructuresKHR(vk::ArrayProxy<vk::AccelerationStructureBuildGeometryInfoKHR const> const& infos, vk::ArrayProxy<vk::AccelerationStructureBuildRangeInfoKHR const* const> const& pBuildRangeInfos) {
	Need();
	_handle.buildAccelerationStructuresKHR(infos, pBuildRangeInfos);
}

void VKWrapper::VKCommandBuffer::traceRaysKHR(vk::StridedDeviceAddressRegionKHR const& raygenShaderBindingTable, vk::StridedDeviceAddressRegionKHR const& missShaderBindingTable, vk::StridedDeviceAddressRegionKHR const& hitShaderBindingTable, vk::StridedDeviceAddressRegionKHR const& callableShaderBindingTable, uint32_t width, uint32_t height, uint32_t depth)
{
	Need();
	_handle.traceRaysKHR(raygenShaderBindingTable, missShaderBindingTable, hitShaderBindingTable, callableShaderBindingTable, width, height, depth);
}


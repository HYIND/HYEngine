#include "vkstdafx.h"
#include "VulkanRenderEngine\VKWrapper\VKRenderPass.h"

using namespace VKWrapper;

// ---------- 添加附件 ----------
void RenderPassConfig::AddAttachment(
	vk::Format format,
	vk::SampleCountFlagBits samples,
	vk::AttachmentLoadOp loadOp,
	vk::AttachmentStoreOp storeOp,
	vk::AttachmentLoadOp stencilLoadOp,
	vk::AttachmentStoreOp stencilStoreOp,
	vk::ImageLayout initialLayout,
	vk::ImageLayout finalLayout
) {
	attachments.push_back(
		vk::AttachmentDescription()
		.setFormat(format)
		.setSamples(samples)
		.setLoadOp(loadOp)
		.setStoreOp(storeOp)
		.setStencilLoadOp(stencilLoadOp)
		.setStencilStoreOp(stencilStoreOp)
		.setInitialLayout(initialLayout)
		.setFinalLayout(finalLayout)
	);
}

// ---------- 添加子通道 ----------
void RenderPassConfig::AddSubpass(
	vk::PipelineBindPoint bindPoint,
	const std::vector<vk::AttachmentReference>& colorAttachments,
	const std::vector<vk::AttachmentReference>& depthStencilAttachments,
	const std::vector<vk::AttachmentReference>& inputAttachments,
	const std::vector<vk::AttachmentReference>& resolveAttachments
) {
	// 存储引用向量
	colorRefs.push_back(colorAttachments);
	depthRefs.push_back(depthStencilAttachments);
	inputRefs.push_back(inputAttachments);
	resolveRefs.push_back(resolveAttachments);

	// 获取当前索引
	size_t idx = subpasses.size();

	// 构建 SubpassDescription
	vk::SubpassDescription subpass;
	subpass
		.setFlags({})
		.setPipelineBindPoint(bindPoint)
		.setInputAttachments(inputRefs[idx])
		.setColorAttachments(colorRefs[idx])
		.setPDepthStencilAttachment(depthRefs[idx].empty() ? nullptr : &depthRefs[idx][0])
		.setPreserveAttachments({});

	if (!resolveRefs[idx].empty() && resolveRefs[idx].size() == colorRefs[idx].size())
		subpass.setResolveAttachments(resolveRefs[idx]);

	subpasses.push_back(subpass);
}

// ---------- 添加子通道（简化版：只传附件索引） ----------
void RenderPassConfig::AddSubpass(
	vk::PipelineBindPoint bindPoint,
	const std::vector<uint32_t>& colorAttachmentIndices,
	std::optional<uint32_t> depthStencilAttachmentIndex,
	const std::vector<uint32_t>& inputAttachmentIndices,
	const std::vector<uint32_t>& resolveAttachmentIndices
) {
	// 构建颜色附件引用
	std::vector<vk::AttachmentReference> colorRefs;
	colorRefs.reserve(colorAttachmentIndices.size());
	for (uint32_t idx : colorAttachmentIndices) {
		colorRefs.push_back(
			vk::AttachmentReference()
			.setAttachment(idx)
			.setLayout(vk::ImageLayout::eColorAttachmentOptimal)
		);
	}

	// 构建深度模板附件引用
	std::vector<vk::AttachmentReference> depthRefs;
	if (depthStencilAttachmentIndex.has_value()) {
		depthRefs.push_back(
			vk::AttachmentReference()
			.setAttachment(depthStencilAttachmentIndex.value())
			.setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
		);
	}

	// 构建输入附件引用
	std::vector<vk::AttachmentReference> inputRefs;
	inputRefs.reserve(inputAttachmentIndices.size());
	for (uint32_t idx : inputAttachmentIndices) {
		inputRefs.push_back(
			vk::AttachmentReference()
			.setAttachment(idx)
			.setLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
		);
	}

	// 构建解析附件引用
	std::vector<vk::AttachmentReference> resolveRefs;
	resolveRefs.reserve(resolveAttachmentIndices.size());
	for (uint32_t idx : resolveAttachmentIndices) {
		resolveRefs.push_back(
			vk::AttachmentReference()
			.setAttachment(idx)
			.setLayout(vk::ImageLayout::eColorAttachmentOptimal)
		);
	}

	AddSubpass(bindPoint, colorRefs, depthRefs, inputRefs, resolveRefs);
}

// ---------- 添加依赖 ----------
void RenderPassConfig::AddDependency(
	uint32_t srcSubpass,
	uint32_t dstSubpass,
	vk::PipelineStageFlags srcStageMask,
	vk::PipelineStageFlags dstStageMask,
	vk::AccessFlags srcAccessMask,
	vk::AccessFlags dstAccessMask,
	vk::DependencyFlags dependencyFlags
) {
	dependencies.push_back(
		vk::SubpassDependency()
		.setSrcSubpass(srcSubpass)
		.setDstSubpass(dstSubpass)
		.setSrcStageMask(srcStageMask)
		.setDstStageMask(dstStageMask)
		.setSrcAccessMask(srcAccessMask)
		.setDstAccessMask(dstAccessMask)
		.setDependencyFlags(dependencyFlags)
	);
}

RenderPassConfig RenderPassConfig::CreateDefaultConfig(
	VKCore::VulkanDevice* device,
	vk::Format colorFormat,
	vk::Format depthFormat,
	vk::SampleCountFlagBits samples
) {
	RenderPassConfig config;
	config.device = device;

	config.AddAttachment(
		colorFormat,
		samples,
		vk::AttachmentLoadOp::eClear,
		vk::AttachmentStoreOp::eStore,
		vk::AttachmentLoadOp::eDontCare,
		vk::AttachmentStoreOp::eDontCare,
		vk::ImageLayout::eColorAttachmentOptimal,
		vk::ImageLayout::eColorAttachmentOptimal
	);

	config.AddAttachment(
		depthFormat,
		samples,
		vk::AttachmentLoadOp::eClear,
		vk::AttachmentStoreOp::eDontCare,
		vk::AttachmentLoadOp::eClear,
		vk::AttachmentStoreOp::eDontCare,
		vk::ImageLayout::eDepthStencilAttachmentOptimal,
		vk::ImageLayout::eDepthStencilAttachmentOptimal
	);

	config.AddSubpass(vk::PipelineBindPoint::eGraphics, { 0 }, 1, {}, {});
	config.AddDependency(
		VK_SUBPASS_EXTERNAL,
		0,
		vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
		vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
		vk::AccessFlagBits::eNone,
		vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
		vk::DependencyFlagBits::eByRegion
	);
	config.AddDependency(
		0,
		0,
		vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
		vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
		vk::AccessFlagBits::eNone,
		vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
		vk::DependencyFlagBits::eByRegion
	);

	return config;
}

RenderPassConfig RenderPassConfig::CreateSceenDefaultConfig(VKCore::VulkanDevice* device, vk::Format colorFormat, vk::SampleCountFlagBits samples)
{
	RenderPassConfig config;
	config.device = device;

	config.AddAttachment(
		colorFormat,
		samples,
		vk::AttachmentLoadOp::eClear,
		vk::AttachmentStoreOp::eStore,
		vk::AttachmentLoadOp::eDontCare,
		vk::AttachmentStoreOp::eDontCare,
		vk::ImageLayout::eUndefined,
		vk::ImageLayout::ePresentSrcKHR
	);

	config.AddSubpass(vk::PipelineBindPoint::eGraphics, { 0 }, std::nullopt, {}, {});
	config.AddDependency(
		VK_SUBPASS_EXTERNAL,
		0,
		vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
		vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
		vk::AccessFlagBits::eNone,
		vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
		vk::DependencyFlagBits::eByRegion
	);

	return config;
}

std::tuple<vk::Result, vk::RenderPass> RenderPassConfig::Create()
{
	UpdateSubpassPointers();

	vk::RenderPassCreateInfo createInfo;
	createInfo
		.setFlags({})
		.setAttachments(attachments)
		.setSubpasses(subpasses)
		.setDependencies(dependencies);

	auto [result, renderpass] = device->GetHandle().createRenderPass(createInfo);
	if (result != vk::Result::eSuccess)
	{
		std::cout << std::format("[RenderPassConfig] fail to create RenderPass! Error = {}", to_string(result));
		return { result, VK_NULL_HANDLE };
	}

	return { result, renderpass };
}

void RenderPassConfig::UpdateSubpassPointers() {
	for (size_t i = 0; i < subpasses.size() && i < colorRefs.size(); ++i) {
		auto& subpass = subpasses[i];
		subpass
			.setInputAttachments(inputRefs[i])
			.setColorAttachments(colorRefs[i])
			.setPDepthStencilAttachment(depthRefs[i].empty() ? nullptr : &depthRefs[i][0]);

		if (!resolveRefs[i].empty() && resolveRefs[i].size() == colorRefs[i].size())
			subpass.setResolveAttachments(resolveRefs[i]);
	}
}


VKRenderPass::~VKRenderPass()
{
	Release();
}

VKRenderPass::VKRenderPass(VKRenderPass&& other) noexcept
	: m_device(other.m_device), m_renderPass(other.m_renderPass) {
	other.m_device = nullptr;
	other.m_renderPass = VK_NULL_HANDLE;
}

VKRenderPass& VKRenderPass::operator=(VKRenderPass&& other) noexcept {
	if (this != &other) {
		Release();
		m_device = other.m_device;
		m_renderPass = other.m_renderPass;
		other.m_device = nullptr;
		other.m_renderPass = VK_NULL_HANDLE;
	}
	return *this;
}

bool VKRenderPass::Create(RenderPassConfig& config)
{
	Release();
	m_device = config.device;

	auto [result, renderPass] = config.Create();
	if (result != vk::Result::eSuccess)
	{
		std::cout << std::format("[VKRenderPass] fail to create render pass , Error = {}", to_string(result));
		return false;
	}

	m_renderPass = renderPass;
	return true;
}

void VKRenderPass::Release()
{
	if (m_device && m_renderPass) {
		vkDestroyRenderPass(m_device->GetHandle(), m_renderPass, nullptr);
	}
	m_renderPass = VK_NULL_HANDLE;
	m_device = nullptr;
}

vk::RenderPass VKRenderPass::GetHandle() const { return m_renderPass; }

VKCore::VulkanDevice* VKRenderPass::GetDevice() const { return m_device; }

bool VKRenderPass::IsValid() const { return m_renderPass != VK_NULL_HANDLE; }

void VKRenderPass::BeginPass(std::shared_ptr<VKWrapper::VKCommandBuffer> commandBuffer, vk::RenderPassBeginInfo& beginInfo, vk::SubpassContents subpassContents) const
{
	beginInfo.setRenderPass(m_renderPass);
	commandBuffer->beginRenderPass(beginInfo, subpassContents);
}

void VKRenderPass::BeginPass(std::shared_ptr<VKWrapper::VKCommandBuffer> commandBuffer, VKWrapper::VKFrameBuffer& framebuffer, const vk::Rect2D& renderArea, const std::vector<vk::ClearValue>& clearValues, vk::SubpassContents subpassContents) const
{
	if (!commandBuffer)
		return;
	vk::RenderPassBeginInfo beginInfo;
	beginInfo.setRenderPass(m_renderPass)
		.setFramebuffer(framebuffer.GetHandle())
		.setClearValues(clearValues)
		.setRenderArea(renderArea);
	commandBuffer->beginRenderPass(beginInfo, subpassContents);
}

void VKRenderPass::NextSubPass(std::shared_ptr<VKWrapper::VKCommandBuffer> commandBuffer, vk::SubpassContents subpassContents) const
{
	if (!commandBuffer)
		return;
	commandBuffer->nextSubpass(subpassContents);
}

void VKRenderPass::EndPass(std::shared_ptr<VKWrapper::VKCommandBuffer> commandBuffer) const {
	if (!commandBuffer)
		return;
	commandBuffer->endRenderPass();
}


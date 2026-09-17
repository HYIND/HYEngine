#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/GraphicsPipeline.h"
#include "VulkanRenderEngine/VKContext.h"

GraphicsPipelineConfig::GraphicsPipelineCreateInfoPack::GraphicsPipelineCreateInfoPack()
{
	createInfo.basePipelineIndex = 0;

	inputAssemblyStateCi
		.setFlags({})
		.setTopology(vk::PrimitiveTopology::eTriangleList)
		.setPrimitiveRestartEnable(VK_FALSE);

	rasterizationStateCi
		.setFlags({})
		.setDepthClampEnable(VK_FALSE)
		.setRasterizerDiscardEnable(VK_FALSE)
		.setPolygonMode(vk::PolygonMode::eFill)
		.setCullMode(vk::CullModeFlagBits::eBack)
		.setFrontFace(vk::FrontFace::eCounterClockwise)
		.setDepthBiasEnable(VK_FALSE)
		.setDepthBiasConstantFactor(0.0f)
		.setDepthBiasClamp(0.0f)
		.setDepthBiasSlopeFactor(0.0f)
		.setLineWidth(1.0f);

	multisampleStateCi.setRasterizationSamples(vk::SampleCountFlagBits::e1)
		.setSampleShadingEnable(vk::False)
		.setMinSampleShading(0.f)
		.setPSampleMask(nullptr)
		.setAlphaToCoverageEnable(vk::False)
		.setAlphaToOneEnable(vk::False);

	depthStencilStateCi.setFlags({})
		.setDepthTestEnable(vk::True)
		.setDepthWriteEnable(vk::True)
		.setDepthCompareOp(vk::CompareOp::eLess)
		.setDepthBoundsTestEnable(vk::False)
		.setStencilTestEnable(vk::False)
		.setMinDepthBounds(0.f)
		.setMaxDepthBounds(1.0f);

	//dynamicStates = {
	//	vk::DynamicState::eViewportWithCount,
	//	vk::DynamicState::eScissorWithCount,
	//	vk::DynamicState::eColorBlendEnableEXT,
	//	vk::DynamicState::eColorBlendEquationEXT,
	//	vk::DynamicState::eDepthTestEnable,
	//	vk::DynamicState::eDepthWriteEnable,
	//	vk::DynamicState::eDepthCompareOp,
	//	vk::DynamicState::eStencilTestEnable,
	//	vk::DynamicState::eStencilWriteMask,
	//	vk::DynamicState::eStencilCompareMask,
	//	vk::DynamicState::eStencilOp
	//};

	dynamicStates = {
		vk::DynamicState::eViewportWithCount,
		vk::DynamicState::eScissorWithCount
	};

	SetCreateInfos();
}

GraphicsPipelineConfig::GraphicsPipelineCreateInfoPack::GraphicsPipelineCreateInfoPack(const GraphicsPipelineCreateInfoPack& other) noexcept {
	createInfo = other.createInfo;

	vertexInputStateCi = other.vertexInputStateCi;
	inputAssemblyStateCi = other.inputAssemblyStateCi;
	viewportStateCi = other.viewportStateCi;
	rasterizationStateCi = other.rasterizationStateCi;
	multisampleStateCi = other.multisampleStateCi;
	depthStencilStateCi = other.depthStencilStateCi;
	colorBlendStateCi = other.colorBlendStateCi;
	dynamicStateCi = other.dynamicStateCi;

	vertexInputBindings = other.vertexInputBindings;
	vertexInputAttributes = other.vertexInputAttributes;
	depthAttachmentFormat = other.depthAttachmentFormat;
	stencilAttachmentFormat = other.stencilAttachmentFormat;
	colorAttachmentStates = other.colorAttachmentStates;
	colorAttachmentFormats = other.colorAttachmentFormats;
	dynamicStates = other.dynamicStates;

	UpdateAllProps();
}

void GraphicsPipelineConfig::GraphicsPipelineCreateInfoPack::UpdateAllProps() {

	vertexInputStateCi.setVertexBindingDescriptions(vertexInputBindings)
		.setVertexAttributeDescriptions(vertexInputAttributes);

	viewportStateCi
		.setViewportCount(0)
		.setScissorCount(0);

	colorBlendStateCi.setAttachments(colorAttachmentStates);

	dynamicStateCi.setDynamicStates(dynamicStates);

	renderingInfo
		.setColorAttachmentFormats(colorAttachmentFormats)
		.setDepthAttachmentFormat(depthAttachmentFormat)
		.setStencilAttachmentFormat(stencilAttachmentFormat);

	SetCreateInfos();
}

void GraphicsPipelineConfig::GraphicsPipelineCreateInfoPack::SetCreateInfos() {
	createInfo.setPVertexInputState(&vertexInputStateCi)
		.setPInputAssemblyState(&inputAssemblyStateCi)
		.setPViewportState(&viewportStateCi)
		.setPRasterizationState(&rasterizationStateCi)
		.setPMultisampleState(&multisampleStateCi)
		.setPDepthStencilState(&depthStencilStateCi)
		.setPColorBlendState(&colorBlendStateCi)
		.setPDynamicState(&dynamicStateCi)
		.setRenderPass(nullptr)
		.setSubpass(0)
		.setPNext(renderingInfo);
}

void GraphicsPipelineConfig::UpdateAllProps() {
	pack.UpdateAllProps();
}

//void GraphicsPipelineConfig::SetRenderPass(vk::RenderPass pass) {
//	renderPass = pass;
//	pack.createInfo.setRenderPass(renderPass);
//}

void GraphicsPipelineConfig::SetTopology(vk::PrimitiveTopology topology) { pack.inputAssemblyStateCi.topology = topology; }
void GraphicsPipelineConfig::SetPolygonMode(vk::PolygonMode mode) { pack.rasterizationStateCi.polygonMode = mode; }
void GraphicsPipelineConfig::SetCullMode(vk::CullModeFlags mode) { pack.rasterizationStateCi.cullMode = mode; }
void GraphicsPipelineConfig::SetFrontFace(vk::FrontFace face) { pack.rasterizationStateCi.frontFace = face; }
void GraphicsPipelineConfig::SetDepthTest(bool enable, bool write, vk::CompareOp op) {
	pack.depthStencilStateCi.depthTestEnable = enable ? VK_TRUE : VK_FALSE;
	pack.depthStencilStateCi.depthWriteEnable = write ? VK_TRUE : VK_FALSE;
	pack.depthStencilStateCi.depthCompareOp = op;
}

void GraphicsPipelineConfig::AddColorAttachment(vk::Format format, vk::PipelineColorBlendAttachmentState state)
{
	pack.colorAttachmentStates.push_back(state);
	pack.colorAttachmentFormats.push_back(format);
}

void GraphicsPipelineConfig::SetDepthStencilAttachmentFormat(vk::Format format)
{
	pack.depthAttachmentFormat = format;
	pack.stencilAttachmentFormat = format;
}

void GraphicsPipelineConfig::SetDepthAttachmentFormat(vk::Format format)
{
	pack.depthAttachmentFormat = format;
}

void GraphicsPipelineConfig::SetStencilAttachmentFormat(vk::Format format)
{
	pack.stencilAttachmentFormat = format;
}

void GraphicsPipelineConfig::SetSampleCount(vk::SampleCountFlagBits samples) {
	pack.multisampleStateCi.setRasterizationSamples(samples);
}

void GraphicsPipelineConfig::AddDynamicState(vk::DynamicState state) {
	pack.dynamicStates.push_back(state);
}

void GraphicsPipelineConfig::AddVertexInputAttributeDescription(const vk::VertexInputAttributeDescription& desc) {
	pack.vertexInputAttributes.push_back(desc);
}

void GraphicsPipelineConfig::AddVertexInputAttributeDescription(const std::vector<vk::VertexInputAttributeDescription>& desc) {
	pack.vertexInputAttributes.append_range(desc);
}

void GraphicsPipelineConfig::AddVertexInputBindingDescription(const vk::VertexInputBindingDescription& desc) {
	pack.vertexInputBindings.push_back(desc);
}

void GraphicsPipelineConfig::AddVertexInputBindingDescription(const std::vector<vk::VertexInputBindingDescription>& desc) {
	pack.vertexInputBindings.append_range(desc);
}

bool GraphicsPipelineConfig::Validate() const
{
	if (vertexPath.empty() || fragmentPath.empty()) {
		std::cout << std::format("[GraphicsPipelineConfig Warning] vertexPath or fragmentPath is emtpy!\n");
		return false;
	}
	if (pack.vertexInputBindings.empty() || pack.vertexInputAttributes.empty()) {
		std::cout << std::format("[GraphicsPipelineConfig Warning] vertexInputBindings or vertexInputAttributes is emtpy!\n");
	}
	if (pack.colorAttachmentStates.empty()) {
		std::cout << std::format("[GraphicsPipelineConfig Warning] colorAttachmentStates is emtpy!\n");
	}
	//if (renderPass == VK_NULL_HANDLE) {
	//	std::cout << std::format("[GraphicsPipelineConfig Error] renderPass is VK_NULL_HANDLE!\n");
	//	return false;
	//}
	return true;
}

GraphicsPipeline::GraphicsPipeline() {
	m_bindPoint = vk::PipelineBindPoint::eGraphics;
	m_bindStage = Texture2D::BindStage::Graphics;
}

GraphicsPipeline::~GraphicsPipeline()
{
	Release();
}

bool GraphicsPipeline::Create(
	GraphicsPipelineConfig& config,
	VKCore::VulkanDevice* device
)
{
	Release();

	config.UpdateAllProps();

	m_device = device;

	// 创建 PipelineLayout
	if (CreatePipelineLayout(config) != vk::Result::eSuccess) {
		return false;
	}

	// 创建 Pipeline
	return CreatePipeline(config);
}

void GraphicsPipeline::Release()
{
	// 基类 Destroy 会销毁 pipeline、layout、descriptorSetLayout
	// 这里只销毁 ShaderModule
	if (m_device)
	{
		if (m_vertModule) m_device->GetHandle().destroyShaderModule(m_vertModule);
		if (m_fragModule) m_device->GetHandle().destroyShaderModule(m_fragModule);
		if (m_geomModule) m_device->GetHandle().destroyShaderModule(m_geomModule);
		if (m_tessControlModule) m_device->GetHandle().destroyShaderModule(m_tessControlModule);
		if (m_tessEvalModule) m_device->GetHandle().destroyShaderModule(m_tessEvalModule);

	}

	m_vertModule = VK_NULL_HANDLE;
	m_fragModule = VK_NULL_HANDLE;
	m_geomModule = VK_NULL_HANDLE;
	m_tessControlModule = VK_NULL_HANDLE;
	m_tessEvalModule = VK_NULL_HANDLE;

	m_shaderStages.clear();

	Pipeline::Release();
}


bool GraphicsPipeline::CreateShaderStages(GraphicsPipelineConfig& config)
{
	static auto GetStageCreateInfo = [](vk::ShaderModule module, vk::ShaderStageFlagBits stage) -> vk::PipelineShaderStageCreateInfo
		{
			static const char* entry = "main";
			vk::PipelineShaderStageCreateInfo createInfo;
			createInfo.setStage(stage)
				.setModule(module)
				.setPName(entry);
			return createInfo;
		};


	// 顶点着色器
	if (!config.vertexPath.empty()) {
		auto code = ReadSPIRVFromSourcePath(config.vertexPath, ShaderType::Vertex, config.defines);
		if (CreateShaderModule(code, m_vertModule) != vk::Result::eSuccess) return false;
		m_shaderStages.push_back(GetStageCreateInfo(m_vertModule, vk::ShaderStageFlagBits::eVertex));
	}

	// 片段着色器
	if (!config.fragmentPath.empty()) {
		auto code = ReadSPIRVFromSourcePath(config.fragmentPath, ShaderType::Fragment, config.defines);
		if (CreateShaderModule(code, m_fragModule) != vk::Result::eSuccess) return false;
		m_shaderStages.push_back(GetStageCreateInfo(m_fragModule, vk::ShaderStageFlagBits::eFragment));
	}

	// 几何着色器
	if (!config.geometryPath.empty()) {
		auto code = ReadSPIRVFromSourcePath(config.geometryPath, ShaderType::Geometry, config.defines);
		if (CreateShaderModule(code, m_geomModule) != vk::Result::eSuccess) return false;
		m_shaderStages.push_back(GetStageCreateInfo(m_geomModule, vk::ShaderStageFlagBits::eGeometry));
	}

	return true;
}

bool GraphicsPipeline::CreatePipeline(GraphicsPipelineConfig& config)
{
	// 创建 ShaderModule
	if (!CreateShaderStages(config)) {
		return false;
	}

	auto& pipelineInfo = config.pack.createInfo;
	pipelineInfo
		.setStages(m_shaderStages)
		.setLayout(m_layout);

	auto [result, pipeline] = m_device->GetHandle().createGraphicsPipeline(vk::PipelineCache(), pipelineInfo);
	if (result != vk::Result::eSuccess)
	{
		std::cout << std::format("[GraphicsPipeline] fail to create Pipeline! error = {}\n", to_string(result));
		return false;
	}
	m_pipeline = pipeline;
	return true;
}

void GraphicsPipeline::Bind(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, BindingRecord& bindingRecord) {
	if (!cmdBuffer)
		return;
	Pipeline::Bind(cmdBuffer, bindingRecord);
	cmdBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline);
}

vk::ShaderModule GraphicsPipeline::GetVertexModule() const { return m_vertModule; }

vk::ShaderModule GraphicsPipeline::GetFragmentModule() const { return m_fragModule; }

vk::ShaderModule GraphicsPipeline::GetGeometryModule() const { return m_geomModule; }

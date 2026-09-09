#pragma once

#include "Pipeline.h"

struct GraphicsPipelineConfig : public PipelineConfig
{
	inline static vk::PipelineColorBlendAttachmentState DefaultColorAttachmentBlend =
		vk::PipelineColorBlendAttachmentState()
		.setColorBlendOp(vk::BlendOp::eAdd)
		.setAlphaBlendOp(vk::BlendOp::eAdd)
		.setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
		.setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
		.setSrcAlphaBlendFactor(vk::BlendFactor::eSrcAlpha)
		.setDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
		.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

	struct GraphicsPipelineCreateInfoPack
	{
		vk::GraphicsPipelineCreateInfo createInfo;

		// Vertex Input
		vk::PipelineVertexInputStateCreateInfo vertexInputStateCi;
		std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes;
		std::vector<vk::VertexInputBindingDescription> vertexInputBindings;

		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyStateCi; //Input Assembly

		vk::PipelineViewportStateCreateInfo viewportStateCi;			//Viewport

		vk::PipelineRasterizationStateCreateInfo rasterizationStateCi;	//Rasterization
		vk::PipelineMultisampleStateCreateInfo multisampleStateCi;		//Multisample

		// 深度模板附件状态
		vk::PipelineDepthStencilStateCreateInfo depthStencilStateCi;
		vk::Format depthAttachmentFormat = vk::Format::eD24UnormS8Uint;
		vk::Format stencilAttachmentFormat = vk::Format::eD24UnormS8Uint;

		// 颜色附件的ColorBlend参数
		vk::PipelineColorBlendStateCreateInfo colorBlendStateCi;
		std::vector<vk::PipelineColorBlendAttachmentState> colorAttachmentStates;	//颜色附件状态
		std::vector<vk::Format> colorAttachmentFormats;								//颜色附件格式

		// 动态渲染
		vk::PipelineRenderingCreateInfo renderingInfo;

		// Dynamic
		vk::PipelineDynamicStateCreateInfo dynamicStateCi;
		std::vector<vk::DynamicState> dynamicStates;

		GraphicsPipelineCreateInfoPack();
		GraphicsPipelineCreateInfoPack(const GraphicsPipelineCreateInfoPack& other) noexcept;

		void UpdateAllProps();

	private:
		void SetCreateInfos();
	};

	GraphicsPipelineCreateInfoPack pack;

	// ---------- 着色器路径 ----------
	std::string vertexPath;
	std::string fragmentPath;
	std::string geometryPath;    // 可选


	// ---------- 固定功能设置 ----------
	void UpdateAllProps();

	//void SetRenderPass(vk::RenderPass pass);

	void AddColorAttachment(vk::Format format = vk::Format::eR8G8B8A8Unorm, vk::PipelineColorBlendAttachmentState state = DefaultColorAttachmentBlend);
	void SetDepthStencilAttachmentFormat(vk::Format format);
	void SetDepthAttachmentFormat(vk::Format format);
	void SetStencilAttachmentFormat(vk::Format format);

	void SetDepthTest(bool enable, bool write, vk::CompareOp op = vk::CompareOp::eLess);
	void AddDynamicState(vk::DynamicState state);

	void SetTopology(vk::PrimitiveTopology topology);
	void SetPolygonMode(vk::PolygonMode mode);
	void SetCullMode(vk::CullModeFlags mode);
	void SetFrontFace(vk::FrontFace face);
	void SetSampleCount(vk::SampleCountFlagBits samples);

	void AddVertexInputAttributeDescription(const vk::VertexInputAttributeDescription& desc);
	void AddVertexInputAttributeDescription(const std::vector<vk::VertexInputAttributeDescription>& desc);
	void AddVertexInputBindingDescription(const vk::VertexInputBindingDescription& desc);
	void AddVertexInputBindingDescription(const std::vector<vk::VertexInputBindingDescription>& desc);

	// 验证配置完整性
	bool Validate() const;

	// 转发父类方法，使其返回子类的引用
	GraphicsPipelineConfig& AddUnifromBuffer(uint32_t binding, uint32_t set = 0) { PipelineConfig::AddUnifromBuffer(binding, set); return *this; }
	GraphicsPipelineConfig& AddStorageBuffer(uint32_t binding, uint32_t set = 0) { PipelineConfig::AddStorageBuffer(binding, set); return *this; }
	GraphicsPipelineConfig& AddUnifromTexture(uint32_t binding, uint32_t set = 0) { PipelineConfig::AddUnifromTexture(binding, set); return *this; }
	GraphicsPipelineConfig& AddUnifromBufferArray(uint32_t binding, uint32_t count = 1, uint32_t set = 0) { PipelineConfig::AddUnifromBufferArray(binding, count, set); return *this; }
	GraphicsPipelineConfig& AddUnifromTextureArray(uint32_t binding, uint32_t count = 1, uint32_t set = 0) { PipelineConfig::AddUnifromTextureArray(binding, count, set); return *this; }
	GraphicsPipelineConfig& AddUnifromVariableTextureArray(uint32_t binding, uint32_t maxCount = 1, uint32_t set = 0) { PipelineConfig::AddUnifromVariableTextureArray(binding, maxCount, set); return *this; }
	GraphicsPipelineConfig& AddCameraUnifromDataBinding() { PipelineConfig::AddCameraUnifromDataBinding(); return *this; }
	GraphicsPipelineConfig& AddBindlessMaterialTextureBinding() { PipelineConfig::AddBindlessMaterialTextureBinding(); return *this; }
	GraphicsPipelineConfig& AddLightDataBinding() { PipelineConfig::AddLightDataBinding(); return *this; }
	GraphicsPipelineConfig& AddAnimationDataBinding() { PipelineConfig::AddAnimationDataBinding(); return *this; }
};

class GraphicsPipeline : public Pipeline
{
public:
	GraphicsPipeline();
	~GraphicsPipeline() override;

	// ---------- 创建 ----------
	bool Create(
		GraphicsPipelineConfig& config,
		VKCore::VulkanDevice* device = VKCONTEXT->GetDevice().get()
	);

	virtual void Release() override;
	void Bind(std::shared_ptr<VKWrapper::VKCommandBuffer> cmdBuffer) override;

	vk::RenderPass GetRenderPass() const;
	vk::ShaderModule GetVertexModule() const;
	vk::ShaderModule GetFragmentModule() const;
	vk::ShaderModule GetGeometryModule() const;

private:
	bool CreateShaderStages(GraphicsPipelineConfig& config);
	bool CreatePipeline(GraphicsPipelineConfig& config);

	// ShaderModule
	vk::ShaderModule m_vertModule = VK_NULL_HANDLE;
	vk::ShaderModule m_fragModule = VK_NULL_HANDLE;
	vk::ShaderModule m_geomModule = VK_NULL_HANDLE;
	vk::ShaderModule m_tessControlModule = VK_NULL_HANDLE;
	vk::ShaderModule m_tessEvalModule = VK_NULL_HANDLE;

	// 着色器阶段（临时存储，创建管线时使用）
	std::vector<vk::PipelineShaderStageCreateInfo> m_shaderStages;
};

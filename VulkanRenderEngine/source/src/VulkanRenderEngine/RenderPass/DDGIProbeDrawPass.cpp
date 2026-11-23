#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/DDGIProbeDrawPass.h"
#include "VulkanRenderEngine/General/RenderHelper.h"
#include "VulkanRenderEngine/GlobalConfig.h"

namespace
{
	using DDGIFramePtr = std::shared_ptr<const DDGIFrame>;
	constexpr const char* kFrameKey = "ddgiDrawFrame";
}

DDGIProbeDrawPass::DDGIProbeDrawPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath)
{
	GraphicsPipelineConfig config;
	config.vertexPath = vertexShaderPath;
	config.fragmentPath = fragmentShaderPath;
	DDGIAddLayoutDefines(config);

	vk::VertexInputAttributeDescription attribute;
	attribute.setLocation(0)
		.setBinding(0)
		.setFormat(vk::Format::eR32G32B32Sfloat)
		.setOffset(0);

	vk::VertexInputBindingDescription bindingDescription;
	bindingDescription.setBinding(0)
		.setStride(sizeof(glm::vec3))
		.setInputRate(vk::VertexInputRate::eVertex);

	config.AddVertexInputAttributeDescription(attribute);
	config.AddVertexInputBindingDescription(bindingDescription);

	config.AddColorAttachment(vk::Format::eR16G16B16A16Sfloat);
	config.SetDepthStencilAttachmentFormat(vk::Format::eD24UnormS8Uint);
	config.SetDepthTest(true, true);

	config
		.AddCameraUnifromDataBinding()
		.AddUnifromBuffer(0)		// DDGIUniforms
		.AddStorageBuffer(1)		// 体积表
		.AddStorageBuffer(2)		// probeInfo
		.AddUnifromTexture(3)		// 辐照度图集
		.AddUnifromTexture(4)		// 深度图集
		.AddUnifromTexture(5);		// probeData

	if (config.Validate())
		_shader.Create(config);

	// 低模球就够了：探针数可能上万
	auto sphereModel = RenderHelper::GetSphereModel(0.5f, 16, 8);
	auto& sphereMesh = sphereModel->GetMeshInfos()[0].mesh;

	std::vector<glm::vec3> vertices;
	vertices.reserve(sphereMesh->GetVertices().size());
	for (auto& v : sphereMesh->GetVertices())
		vertices.push_back(v.Position);
	const auto& indices = sphereMesh->GetIndices();
	_indexCount = uint32_t(indices.size());

	_vertexBuffer = std::make_shared<VertexBufferBlock>();
	_indexBuffer = std::make_shared<IndexBufferBlock>();
	_vertexBuffer->WriteData(vertices.data(), vertices.size() * sizeof(glm::vec3));
	_indexBuffer->WriteData(indices.data(), indices.size() * sizeof(unsigned int));
}

bool DDGIProbeDrawPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return state.option.flags.ddgiOn && state.option.flags.ddgiProbeDrawOn
		&& GlobalConfig::RTCoreEnable && _sharedState != nullptr && _indexCount > 0;
}

void DDGIProbeDrawPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	registry.Store(kFrameKey, DDGIFramePtr{});
	if (!ShouldExecute(registry, state))
		return;
	registry.Store(kFrameKey, _sharedState->Resolve(state));
}

void DDGIProbeDrawPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	if (!ShouldExecute(registry, state))
		return;

	auto* framePtr = registry.TryLoad<DDGIFramePtr>(kFrameKey);
	if (!framePtr || !*framePtr)
		return;
	const DDGIFramePtr frameHolder = *framePtr;
	const DDGIFrame& frame = *frameHolder;
	registry.Store(kFrameKey, DDGIFramePtr{});
	if (!frame.enabled || frame.totalProbes == 0)
		return;

	auto irradianceAtlas = ctx.GetInput(0);
	auto depthAtlas = ctx.GetInput(1);
	auto probeData = ctx.GetInput(2);
	auto targetColorBuffer = ctx.GetExternal(0);
	auto targetDepthBuffer = ctx.GetFrameLocal(0);

	for (auto* tex : { &irradianceAtlas, &depthAtlas, &probeData, &targetColorBuffer, &targetDepthBuffer })
	{
		if (!*tex || (*tex)->IsEmpty())
			return;
	}

	auto ubo = frame.uniformBlock;

	auto cmd = cmdCtx.GetCmd();

	targetColorBuffer->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	targetDepthBuffer->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);

	DynamicRenderInfo info;
	info.AddColorAttachment(targetColorBuffer->GetImageView(vk::ImageAspectFlagBits::eColor), vk::AttachmentLoadOp::eLoad)
		.AddDepthStencilAttachment(targetDepthBuffer->GetImageView(vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil), vk::AttachmentLoadOp::eLoad)
		.SetRenderArea(state.framebuffer.width, state.framebuffer.height);

	DynamicViewport viewport(state.framebuffer.width, state.framebuffer.height);

	GraphicsBindingRecord binding;
	binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
	binding.SetUniformBlock(ubo, 0);
	binding.SetStorageBlock(frame.volumeTable, 1);
	binding.SetStorageBlock(frame.probeInfo, 2);
	binding.SetUniformTexture(irradianceAtlas, vk::ImageAspectFlagBits::eColor, 3);
	binding.SetUniformTexture(depthAtlas, vk::ImageAspectFlagBits::eColor, 4);
	binding.SetUniformTexture(probeData, vk::ImageAspectFlagBits::eColor, 5);

	cmd->beginRendering(info);
	cmd->setDynamicViewport(viewport);

	_shader.Bind(cmd, binding);
	cmd->bindVertexBuffers(_vertexBuffer);
	cmd->bindIndexBuffer(_indexBuffer);
	cmd->drawIndexed(_indexCount, frame.totalProbes, 0, 0, 0);

	cmd->endRendering();
	cmd->SubmitToQueue();
}

void DDGIProbeDrawPass::SetSharedState(std::shared_ptr<DDGISharedState> sharedState)
{
	_sharedState = std::move(sharedState);
}

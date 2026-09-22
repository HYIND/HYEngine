#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/LightDrawPass.h"
#include "VulkanRenderEngine/General/RenderHelp.h"

struct alignas(16) TransformAndColor
{
	glm::mat4 model;
	glm::vec3 color;
};

LightDrawPass::LightDrawPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath)
{
	GraphicsPipelineConfig config;
	config.vertexPath = vertexShaderPath;
	config.fragmentPath = fragmentShaderPath;

	vk::VertexInputAttributeDescription attribute;
	attribute.setLocation(0)
		.setBinding(0)
		.setFormat(vk::Format::eR32G32B32Sfloat)
		.setOffset(offsetof(Vertex, Position));

	vk::VertexInputBindingDescription bindingDescription;
	bindingDescription.setBinding(0)
		.setStride(sizeof(glm::vec3))
		.setInputRate(vk::VertexInputRate::eVertex);

	config.AddVertexInputAttributeDescription(attribute);
	config.AddVertexInputBindingDescription(bindingDescription);

	config.AddColorAttachment(vk::Format::eR16G16B16A16Sfloat);
	config.SetDepthStencilAttachmentFormat(vk::Format::eD24UnormS8Uint);

	config
		.AddCameraUnifromDataBinding()
		.AddStorageBuffer(0);

	if (config.Validate())
		_shader.Create(config);

	auto cubemodel = GetCubeModel(glm::vec3(1.0f), 1.0f);
	auto spheremodel = GetSphereModel(0.5f, 72, 36);

	auto& cubeMesh = cubemodel->getMeshInfos()[0].mesh;
	auto& sphereMesh = spheremodel->getMeshInfos()[0].mesh;

	std::vector<glm::vec3> vertex;
	std::vector<unsigned int> indices;
	vertex.reserve(cubeMesh->GetVertices().size() + sphereMesh->GetVertices().size());
	indices.reserve(cubeMesh->GetIndices().size() + sphereMesh->GetIndices().size());

	_cubeCommandTemplate.instanceCount = 1;
	_cubeCommandTemplate.firstIndex = 0;
	_cubeCommandTemplate.vertexOffset = 0;
	_cubeCommandTemplate.indexCount = cubeMesh->GetIndices().size();

	_sphereCommandTemplate.instanceCount = 1;
	_sphereCommandTemplate.firstIndex = cubeMesh->GetIndices().size();
	_sphereCommandTemplate.vertexOffset = cubeMesh->GetVertices().size();
	_sphereCommandTemplate.indexCount = sphereMesh->GetIndices().size();


	for (auto& ver : cubeMesh->GetVertices())
		vertex.push_back(ver.Position);
	for (auto& ver : sphereMesh->GetVertices())
		vertex.push_back(ver.Position);
	indices.append_range(cubeMesh->GetIndices());
	indices.append_range(sphereMesh->GetIndices());

	_vertexBuffer = std::make_shared<VertexBufferBlock>();
	_indexBuffer = std::make_shared<IndexBufferBlock>();

	_vertexBuffer->WriteData(vertex.data(), vertex.size() * sizeof(glm::vec3));
	_indexBuffer->WriteData(indices.data(), indices.size() * sizeof(unsigned int));

}

bool LightDrawPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state) {
	return
		state.option.flags.lightDrawOn
		&&
		(!state.lights.dirLightInfos.empty() || !state.lights.pointLightInfos.empty() || !state.lights.spotLightInfos.empty());
}

void LightDrawPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!ShouldExecute(registry, state))
		return;

	std::vector<IndirectDrawCommand>& commands = *registry.Get<std::vector<IndirectDrawCommand>>("commands");

	commands.reserve(
		state.lights.dirLightInfos.size()
		+ state.lights.pointLightInfos.size()
		+ state.lights.spotLightInfos.size()
	);

	std::vector<TransformAndColor> transAndColors;
	transAndColors.reserve(
		state.lights.dirLightInfos.size()
		+ state.lights.pointLightInfos.size()
		+ state.lights.spotLightInfos.size()
	);

	auto addCubeCommand = [&](uint32_t index)-> void {
		auto cmd = _cubeCommandTemplate;
		cmd.firstInstance = index;
		commands.push_back(cmd);
		};

	auto addSphereCommand = [&](uint32_t index)-> void {
		auto cmd = _sphereCommandTemplate;
		cmd.firstInstance = index;
		commands.push_back(cmd);
		};

	uint32_t index = 0;

	for (auto& info : state.lights.dirLightInfos)
	{
		if (!info || !info->light || !info->renderCube)
			continue;

		auto& light = info->light;

		glm::mat4 model = glm::mat4(1.0f);
		model = glm::translate(model, state.camera.position - light->getDirection() * state.camera.farPlane * 0.95f);
		model = glm::scale(model, glm::vec3(30.f * (state.camera.farPlane / 500.f)));
		transAndColors.push_back(TransformAndColor{ .model = model, .color = info->light->getColor() });
		addSphereCommand(index++);
	}
	for (auto& info : state.lights.pointLightInfos)
	{
		if (!info || !info->light || !info->renderCube)
			continue;

		auto& light = info->light;

		glm::mat4 model = glm::mat4(1.0f);
		model = glm::translate(model, light->getPosition());
		model = glm::scale(model, glm::vec3(0.15f));
		transAndColors.push_back(TransformAndColor{ .model = model, .color = info->light->getColor() });
		addCubeCommand(index++);
	}
	for (auto& info : state.lights.spotLightInfos)
	{
		if (!info || !info->light || !info->renderCube)
			continue;

		auto& light = info->light;

		glm::mat4 model = glm::mat4(1.0f);
		model = glm::translate(model, light->getPosition());
		model = glm::scale(model, glm::vec3(0.02f));
		transAndColors.push_back(TransformAndColor{ .model = model, .color = info->light->getColor() });
		addCubeCommand(index++);
	}

	std::shared_ptr<StorageBlock> transformAndColors_ssbo = registry.GetStorageBlock("transformAndColors_ssbo");
	std::shared_ptr<IndirectBufferBlock> indirectBuffer = registry.GetIndirectBlock("indirectBuffer");

	if (!commands.empty() || !transAndColors.empty())
	{
		auto cmd = VKCONTEXT->GetCommandBuffer();
		transformAndColors_ssbo->WriteDataAsync(cmd, transAndColors.data(), transAndColors.size() * sizeof(TransformAndColor));
		indirectBuffer->WriteDataAsync(cmd, commands.data(), commands.size() * sizeof(IndirectDrawCommand));
		cmd->SubmitNowAndWait();
	}
}

void LightDrawPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	auto& commands = *registry.Get<std::vector<IndirectDrawCommand>>("commands");
	if (commands.empty())
		return;

	auto targetColorBuffer = ctx.GetExternal(0);
	auto targetDepthBuffer = ctx.GetExternal(1);

	auto cmd = cmdCtx.GetCmd();

	targetColorBuffer->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	targetDepthBuffer->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);

	DynamicRenderInfo info;
	info.AddColorAttachment(targetColorBuffer->GetImageView(vk::ImageAspectFlagBits::eColor), vk::AttachmentLoadOp::eLoad)
		.AddDepthStencilAttachment(targetDepthBuffer->GetImageView(vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil), vk::AttachmentLoadOp::eLoad)
		.SetRenderArea(state.framebuffer.width, state.framebuffer.height);

	DynamicViewport viewport(state.framebuffer.width, state.framebuffer.height);

	cmd->beginRendering(info);
	cmd->setDynamicViewport(viewport);

	std::shared_ptr<StorageBlock> transformAndColors_ssbo = registry.GetStorageBlock("transformAndColors_ssbo");
	std::shared_ptr<IndirectBufferBlock> indirectBuffer = registry.GetIndirectBlock("indirectBuffer");

	GraphicsBindingRecord binding;
	binding.SetStorageBlock(transformAndColors_ssbo, 0);
	binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);

	_shader.Bind(cmd, binding);

	cmd->bindVertexBuffers(_vertexBuffer);
	cmd->bindIndexBuffer(_indexBuffer);
	cmd->drawIndexedIndirect(indirectBuffer, commands.size());

	cmd->endRendering();

	cmd->SubmitToQueue();
}

#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/GeometryPass.h"
#include "VulkanRenderEngine/General/RenderHelp.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"

struct alignas(16) RenderData {
	alignas(16) glm::mat4 curTransform = glm::mat4(1.0f);
	alignas(16) glm::mat4 prevTransform;
	uint32_t materialIndex = 0;
};

GeometryPass::GeometryPass(
	const std::string& staticMeshVertexShaderPath,
	const std::string& staticMeshFragmentShaderPath,
	const std::string& skinnedMeshvertexShaderPath,
	const std::string& skinnedFragmentShaderPath
)
{
	{
		_staticShader = std::make_shared<GraphicsPipeline>();

		GraphicsPipelineConfig config;
		config.vertexPath = staticMeshVertexShaderPath;
		config.fragmentPath = staticMeshFragmentShaderPath;

		config.AddVertexInputAttributeDescription(Vertex::GetVertexInputAttributeDescription());
		config.AddVertexInputBindingDescription(Vertex::GetVertexInputBindingDescription());

		config.AddColorAttachment(vk::Format::eR32G32B32A32Sfloat);
		config.AddColorAttachment(vk::Format::eR16G16B16A16Sfloat);
		config.AddColorAttachment(vk::Format::eR8G8B8A8Unorm);
		config.AddColorAttachment(vk::Format::eR16G16B16A16Sfloat);
		config.AddColorAttachment(vk::Format::eR16G16B16A16Sfloat);
		config.AddColorAttachment(vk::Format::eR8G8B8A8Unorm);

		config.SetDepthStencilAttachmentFormat(vk::Format::eD24UnormS8Uint);

		config
			.AddBindlessMaterialTextureBinding()
			.AddCameraUnifromDataBinding()
			.AddStorageBuffer(2);

		config.AddDynamicState(vk::DynamicState::eCullMode);

		if (config.Validate())
			_staticShader->Create(config);
	}

	{
		_skinnedShader = std::make_shared<GraphicsPipeline>();

		GraphicsPipelineConfig config;
		config.vertexPath = skinnedMeshvertexShaderPath;
		config.fragmentPath = skinnedFragmentShaderPath;

		config.AddVertexInputAttributeDescription(Vertex::GetVertexInputAttributeDescription());
		config.AddVertexInputBindingDescription(Vertex::GetVertexInputBindingDescription());

		config.AddColorAttachment(vk::Format::eR32G32B32A32Sfloat);
		config.AddColorAttachment(vk::Format::eR16G16B16A16Sfloat);
		config.AddColorAttachment(vk::Format::eR8G8B8A8Unorm);
		config.AddColorAttachment(vk::Format::eR16G16B16A16Sfloat);
		config.AddColorAttachment(vk::Format::eR16G16B16A16Sfloat);
		config.AddColorAttachment(vk::Format::eR8G8B8A8Unorm);

		config.SetDepthStencilAttachmentFormat(vk::Format::eD24UnormS8Uint);

		config
			.AddBindlessMaterialTextureBinding()
			.AddAnimationDataBinding()
			.AddCameraUnifromDataBinding()
			.AddPushConstant(sizeof(RenderData));

		config.AddDynamicState(vk::DynamicState::eCullMode);

		if (config.Validate())
			_skinnedShader->Create(config);
	}
}

GeometryPass::~GeometryPass()
{}

DynamicRenderInfo GeometryPass::GenerateDynamicRenderInfo(
	RenderState& state,
	std::shared_ptr<Texture2D>& gPosition,
	std::shared_ptr<Texture2D>& gNormal,
	std::shared_ptr<Texture2D>& gAlbedoOpacity,
	std::shared_ptr<Texture2D>& gMetallicRoughnessMap,
	std::shared_ptr<Texture2D>& gMotionVectorMap,
	std::shared_ptr<Texture2D>& gEmission,
	std::shared_ptr<Texture2D>& gDepthStencilMap
)
{
	auto cmd = VKCONTEXT->GetCommandBuffer();
	gPosition->TransitionLayout(cmd, nullptr, Texture2D::BindStage::Graphics, Texture2D::BindUsage::Output);
	gNormal->TransitionLayout(cmd, nullptr, Texture2D::BindStage::Graphics, Texture2D::BindUsage::Output);
	gAlbedoOpacity->TransitionLayout(cmd, nullptr, Texture2D::BindStage::Graphics, Texture2D::BindUsage::Output);
	gMetallicRoughnessMap->TransitionLayout(cmd, nullptr, Texture2D::BindStage::Graphics, Texture2D::BindUsage::Output);
	gMotionVectorMap->TransitionLayout(cmd, nullptr, Texture2D::BindStage::Graphics, Texture2D::BindUsage::Output);
	gEmission->TransitionLayout(cmd, nullptr, Texture2D::BindStage::Graphics, Texture2D::BindUsage::Output);
	gDepthStencilMap->TransitionLayout(cmd, nullptr, Texture2D::BindStage::Graphics, Texture2D::BindUsage::Output);
	if (cmd->IsRecording())
		VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

	DynamicRenderInfo info;
	info
		.SetRenderArea(state.framebuffer.width, state.framebuffer.height)
		.AddColorAttachment(gPosition->GetImageView())
		.AddColorAttachment(gNormal->GetImageView())
		.AddColorAttachment(gAlbedoOpacity->GetImageView())
		.AddColorAttachment(gMetallicRoughnessMap->GetImageView())
		.AddColorAttachment(gMotionVectorMap->GetImageView())
		.AddColorAttachment(gEmission->GetImageView())
		.AddDepthStencilAttachment(gDepthStencilMap->GetImageView());
	return info;
}

void GeometryPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	auto& models = state.objects.sceneRenderData.opaqueSkinnedModel;
	auto& sorts = state.objects.sceneRenderData.opaqueSkinnedModel_SortIndex;
	sorts.resize(models.size());
	for (int i = 0; i < models.size(); i++)
	{
		auto& item = models[i];
		auto& sort = sorts[i];

		sort.resize(item.models.size());
		std::iota(sort.begin(), sort.end(), 0);

		if (item.models.size() > 1)
		{
			std::sort(std::execution::par_unseq, sort.begin(), sort.end(),
				[&](int index1, int index2)-> bool
				{
					if (item.models[index1].material != item.models[index2].material)
						return item.models[index1].material < item.models[index2].material;
				}
			);
		}
	}
}

void GeometryPass::Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{

	auto gPosition = ctx.GetOutput(0);
	auto gNormal = ctx.GetOutput(1);
	auto gAlbedoOpacity = ctx.GetOutput(2);
	auto gMetallicRoughnessMap = ctx.GetOutput(3);
	auto gMotionVectorMap = ctx.GetOutput(4);
	auto gDepthStencilMap = ctx.GetOutput(5);
	auto gEmission = ctx.GetOutput(6);

	DynamicRenderInfo renderInfo = GenerateDynamicRenderInfo(
		state,
		gPosition, gNormal, gAlbedoOpacity, gMetallicRoughnessMap, gMotionVectorMap, gEmission, gDepthStencilMap
	);
	DynamicViewport viewPort(state.framebuffer.width, state.framebuffer.height);

	auto cmd = VKCONTEXT->GetCommandBuffer();

	RenderSceneGeometryPassStatic(registry, cmd, state, renderInfo, viewPort);
	RenderSceneGeometryPassSkinned(registry, cmd, state, renderInfo, viewPort);

	if (cmd->IsRecording())
		VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
}

void GeometryPass::FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{}

bool GeometryPass::SetupStaticBufferData(
	std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
	GraphicsBindingRecord& binding,
	std::vector<VKRenderObjectData::SceneRenderData::OpaqueMeshItem>& items,
	VKRenderObjectData::RenderIndex& renderIndex,
	std::vector<IndirectDrawCommand>& oneSideCommands,
	std::vector<IndirectDrawCommand>& twoSideCommands
)
{
	if (!cmd)
		return false;

	auto indirectManager = IndirectDrawManager::Instance();
	auto binlessManager = BindlessTextureManager::Instance();

	auto materialssbo = indirectManager->GetMaterialSSBO();
	if (!materialssbo)
		return false;

	binding.SetBindlessMaterialTexture(indirectManager->GetMaterialSSBO(), binlessManager);

	auto renderdata_ssbo = binding.GetStorageBlock(2);
	if (!renderdata_ssbo)
		return false;

	std::vector<RenderData> renderData;
	renderData.resize(renderIndex.oneSideIndex.size() + renderIndex.twoSideIndex.size());

	size_t startInedx = 0;

	for (auto& indices : { renderIndex.oneSideIndex, renderIndex.twoSideIndex })
	{
		auto& commands = indices == renderIndex.oneSideIndex ?
			oneSideCommands
			: twoSideCommands;


		if (indices.empty())
			continue;

		commands.resize(indices.size());

		std::for_each(std::execution::par, indices.begin(), indices.end(),
			[&](const size_t& meshIndex)-> void
			{
				size_t inedx = &meshIndex - indices.data();

				auto& item = items[meshIndex];
				auto& material = item.meshinfo.material;
				auto& mesh = item.meshinfo.mesh;

				IndirectDrawCommand& command = commands[inedx];
				RenderData& data = renderData[startInedx + inedx];

				data.curTransform = item.transform;
				data.prevTransform = item.prevTransform;

				uint64_t materialIndex;
				if (indirectManager->GetMaterialIndex(*material, materialIndex))
					data.materialIndex = materialIndex;

				IndirectDrawMeta meta;
				if (!indirectManager->GetIndirectDrawMeta(*mesh, meta))
					command.instanceCount = 0;
				else
				{
					command.indexCount = meta.indexCount;
					command.firstIndex = meta.firstIndex;
					command.vertexOffset = meta.vertexOffset;
					command.instanceCount = 1;
					command.firstInstance = startInedx + inedx;
				}
			});

		startInedx += indices.size();
	}

	renderdata_ssbo->WriteDataAsync(cmd, renderData.data(), renderData.size() * sizeof(RenderData));
	renderdata_ssbo->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageRead);

	return true;
}

void GeometryPass::RenderSceneGeometryPassStatic(RenderGraph::FrameDataRegistry& registry, std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state, DynamicRenderInfo& renderInfo, DynamicViewport& viewPort)
{

	auto& opaqueMeshes = state.objects.sceneRenderData.opaqueMesh;
	auto& renderIndex = state.objects.sceneRenderData.opaqueMesh_cullRenderIndex;
	if (opaqueMeshes.empty() && renderIndex.oneSideIndex.empty() && renderIndex.twoSideIndex.empty())
		return;

	auto& shader = _staticShader;
	GraphicsBindingRecord binding;

	auto renderdata_ssbo = registry.GetStorageBlock("renderdata_ssbo");
	binding.SetStorageBlock(renderdata_ssbo, 2);

	std::vector<IndirectDrawCommand> oneSideCommands;
	std::vector<IndirectDrawCommand> twoSideCommands;
	if (!SetupStaticBufferData(cmd, binding, opaqueMeshes, renderIndex, oneSideCommands, twoSideCommands))
		return;

	binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);

	auto oneSideCommandBuffer = registry.GetIndirectBlock("oneSideCommandBuffer");
	auto twoSideCommandBuffer = registry.GetIndirectBlock("twoSideCommandBuffer");

	if (!oneSideCommands.empty())
		oneSideCommandBuffer->WriteDataAsync(cmd, oneSideCommands.data(), oneSideCommands.size() * sizeof(IndirectDrawCommand));
	if (!twoSideCommands.empty())
		twoSideCommandBuffer->WriteDataAsync(cmd, twoSideCommands.data(), twoSideCommands.size() * sizeof(IndirectDrawCommand));

	shader->Bind(cmd, binding);

	cmd->setDynamicViewport(viewPort);
	cmd->beginRendering(renderInfo);

	auto indirectManager = IndirectDrawManager::Instance();

	if (!oneSideCommands.empty())
	{
		oneSideCommandBuffer->Barrier(cmd, BufferUsage::TransferWrite);
		cmd->setCullMode(vk::CullModeFlagBits::eBack);
		cmd->bindVertexBuffers(indirectManager->GetVertexBlock());
		cmd->bindIndexBuffer(indirectManager->GetIndexBlock());
		cmd->drawIndexedIndirect(oneSideCommandBuffer, oneSideCommands.size());
	}

	if (!twoSideCommands.empty())
	{
		twoSideCommandBuffer->Barrier(cmd, BufferUsage::TransferWrite);
		cmd->setCullMode(vk::CullModeFlagBits::eNone);
		cmd->bindVertexBuffers(indirectManager->GetVertexBlock());
		cmd->bindIndexBuffer(indirectManager->GetIndexBlock());
		cmd->drawIndexedIndirect(twoSideCommandBuffer, twoSideCommands.size());
	}

	cmd->endRendering();
}

void GeometryPass::RenderSceneGeometryPassSkinned(RenderGraph::FrameDataRegistry& registry, std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state, DynamicRenderInfo& renderInfo, DynamicViewport& viewPort)
{
	//auto& opaqueSinnedModels = state.objects.sceneRenderData.opaqueSkinnedModel;
	//auto& renderIndexArrays = state.objects.sceneRenderData.opaqueSkinnedModel_SortIndex;

	//if (opaqueSinnedModels.empty())
	//	return;

	//auto& shader = _skinnedShader;

	//shader->Bind();

	//std::shared_ptr<Material> cur_Material;
	//glm::mat4 cur_Model = glm::mat4(1.0f);
	//glm::mat4 cur_PreModel = glm::mat4(1.0f);

	//RenderHelp::SetupAnimatorGroupData(shader, {});
	//shader.setMat4("model", cur_Model);
	//shader.setMat4("prevModel", cur_PreModel);

	//for (size_t i = 0; i < opaqueSinnedModels.size(); i++)
	//{
	//	auto& item = opaqueSinnedModels[i];

	//	if (cur_Model != item.transform)
	//	{
	//		shader.setMat4("model", item.transform);
	//		cur_Model = item.transform;
	//	}
	//	if (cur_PreModel != item.prevTransform)
	//	{
	//		shader.setMat4("prevModel", item.prevTransform);
	//		cur_PreModel = item.prevTransform;
	//	}
	//	RenderHelp::SetupAnimatorGroupData(shader, *item.animators);

	//	auto& sort = renderIndexArrays[i];
	//	for (int i = 0; i < sort.size(); i++)
	//	{
	//		auto meshIndex = sort[i];
	//		auto& meshinfo = item.models[meshIndex];
	//		if (cur_Material != meshinfo.material)
	//		{
	//			meshinfo.ApplyMaterialWithSideOption();
	//			cur_Material = meshinfo.material;
	//		}
	//		//meshinfo.mesh->SetDirty();
	//		meshinfo.DrawGeometry(shader);
	//	}
	//}
}
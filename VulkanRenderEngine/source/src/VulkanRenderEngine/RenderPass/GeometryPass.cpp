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

	_oneSideCommandBuffer = std::make_shared<IndirectBufferBlock>();
	_twoSideCommandBuffer = std::make_shared<IndirectBufferBlock>();
}

GeometryPass::~GeometryPass()
{
}

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

void GeometryPass::EarlyExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
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

void GeometryPass::Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassContext& ctx, RenderState& state)
{

	auto gPosition = ctx.GetOutput(0);
	auto gNormal = ctx.GetOutput(1);
	auto gAlbedoOpacity = ctx.GetOutput(2);
	auto gMetallicRoughnessMap = ctx.GetOutput(3);
	auto gMotionVectorMap = ctx.GetOutput(4);
	auto gDepthStencilMap = ctx.GetOutput(5);
	auto gEmission = ctx.GetOutput(6);

	SetupIndirecDrawMaterial(state);

	DynamicRenderInfo renderInfo = GenerateDynamicRenderInfo(
		state,
		gPosition, gNormal, gAlbedoOpacity, gMetallicRoughnessMap, gMotionVectorMap, gEmission, gDepthStencilMap
	);
	DynamicViewport viewPort(state.framebuffer.width, state.framebuffer.height);

	auto cmd1 = VKCONTEXT->GetCommandBuffer();
	auto cmd2 = VKCONTEXT->GetCommandBuffer();

	auto fence1 = std::make_shared<VKWrapper::VKFence>(VKCONTEXT->GetDevice().get());
	auto fence2 = std::make_shared<VKWrapper::VKFence>(VKCONTEXT->GetDevice().get());

	RenderSceneGeometryPassStatic(cmd1, state, renderInfo, viewPort);
	RenderSceneGeometryPassSkinned(cmd2, state, renderInfo, viewPort);

	VKCONTEXT->SubmitCommandImmediately(cmd1, {}, fence1);

	fence1->Wait();
}

void GeometryPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
}

void GeometryPass::FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
}

void GeometryPass::SetupIndirecDrawMaterial(RenderState& state)
{
	auto indirectManager = IndirectDrawManager::Instance();

	{
		auto items = state.objects.sceneRenderData.opaqueMesh;
		for (auto& item : items)
		{
			auto& material = item.meshinfo.material;
			if (material->GetNeedUpdateIndirectDraw())
			{
				indirectManager->setupMaterial(*material);
				material->SetNeedUpdateIndirectDraw(false);
			}
		}
	}

	{
		auto items = state.objects.sceneRenderData.opaqueSkinnedModel;
		for (auto& item : items)
		{
			for (auto& info : item.models)
			{
				auto& material = info.material;
				if (material->GetNeedUpdateIndirectDraw())
				{
					indirectManager->setupMaterial(*material);
					material->SetNeedUpdateIndirectDraw(false);
				}
			}
		}
	}

}

bool GeometryPass::SetupStaticBufferData(
	std::shared_ptr<GraphicsPipeline>& shader,
	std::vector<VKRenderObjectData::SceneRenderData::OpaqueMeshItem>& items,
	VKRenderObjectData::RenderIndex& renderIndex,
	std::vector<IndirectDrawCommand>& oneSideCommands,
	std::vector<IndirectDrawCommand>& twoSideCommands
)
{

	auto indirectManager = IndirectDrawManager::Instance();
	auto binlessManager = BindlessTextureManager::Instance();

	auto materialssbo = indirectManager->GetMaterialSSBO();
	if (!materialssbo)
		return false;

	shader->SetStorageBlock(materialssbo, 0, 2);
	shader->SetUniformTextureArray(binlessManager, 1, 2);

	auto renderdata_ssbo = shader->GetStorageBlock(2);
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

	renderdata_ssbo->WriteData(renderData.data(), renderData.size() * sizeof(RenderData));

	return true;
}

void GeometryPass::RenderSceneGeometryPassStatic(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state, DynamicRenderInfo& renderInfo, DynamicViewport& viewPort)
{
	auto& opaqueMeshes = state.objects.sceneRenderData.opaqueMesh;
	auto& renderIndex = state.objects.sceneRenderData.opaqueMesh_cullRenderIndex;
	auto& shader = _staticShader;

	if (opaqueMeshes.empty() && renderIndex.oneSideIndex.empty() && renderIndex.twoSideIndex.empty())
		return;


	std::vector<IndirectDrawCommand> oneSideCommands;
	std::vector<IndirectDrawCommand> twoSideCommands;
	if (!SetupStaticBufferData(shader, opaqueMeshes, renderIndex, oneSideCommands, twoSideCommands))
		return;

	shader->SetUniformBlock(state.camera.curUBO, GeneralBindingPoint::Camera_Cur);
	shader->SetUniformBlock(state.camera.prevUBO, GeneralBindingPoint::Camera_Prev);

	shader->Bind(cmd);

	cmd->setDynamicViewport(viewPort);
	cmd->beginRendering(renderInfo);

	auto indirectManager = IndirectDrawManager::Instance();

	if (!oneSideCommands.empty())
	{
		_oneSideCommandBuffer->WriteData(oneSideCommands.data(), oneSideCommands.size() * sizeof(IndirectDrawCommand));
		cmd->setCullMode(vk::CullModeFlagBits::eBack);
		cmd->bindVertexBuffers(indirectManager->GetVertexBlock());
		cmd->bindIndexBuffer(indirectManager->GetIndexBlock());
		cmd->drawIndexedIndirect(_oneSideCommandBuffer, oneSideCommands.size());
	}

	if (!twoSideCommands.empty())
	{
		_twoSideCommandBuffer->WriteData(twoSideCommands.data(), twoSideCommands.size() * sizeof(IndirectDrawCommand));
		cmd->setCullMode(vk::CullModeFlagBits::eNone);
		cmd->bindVertexBuffers(indirectManager->GetVertexBlock());
		cmd->bindIndexBuffer(indirectManager->GetIndexBlock());
		cmd->drawIndexedIndirect(_twoSideCommandBuffer, twoSideCommands.size());
	}

	cmd->endRendering();
}

void GeometryPass::RenderSceneGeometryPassSkinned(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state, DynamicRenderInfo& renderInfo, DynamicViewport& viewPort)
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
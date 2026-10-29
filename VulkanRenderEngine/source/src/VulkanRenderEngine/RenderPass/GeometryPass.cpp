#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/GeometryPass.h"
#include "VulkanRenderEngine/General/RenderHelper.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"

GeometryPass::GeometryPass(
	const std::string& staticMeshVertexShaderPath,
	const std::string& staticMeshFragmentShaderPath
)
{
	{
		_staticShader = std::make_shared<GraphicsPipeline>();

		GraphicsPipelineConfig config;
		config.vertexPath = staticMeshVertexShaderPath;
		config.fragmentPath = staticMeshFragmentShaderPath;

		config.AddVertexInputAttributeDescription(StaticMesh::GetVertexInputAttributeDescription());
		config.AddVertexInputBindingDescription(StaticMesh::GetVertexInputBindingDescription());

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
	DynamicRenderInfo info;
	info
		.SetRenderArea(state.framebuffer.width, state.framebuffer.height)
		.AddColorAttachment(gPosition->GetImageView(vk::ImageAspectFlagBits::eColor))
		.AddColorAttachment(gNormal->GetImageView(vk::ImageAspectFlagBits::eColor))
		.AddColorAttachment(gAlbedoOpacity->GetImageView(vk::ImageAspectFlagBits::eColor))
		.AddColorAttachment(gMetallicRoughnessMap->GetImageView(vk::ImageAspectFlagBits::eColor))
		.AddColorAttachment(gMotionVectorMap->GetImageView(vk::ImageAspectFlagBits::eColor))
		.AddColorAttachment(gEmission->GetImageView(vk::ImageAspectFlagBits::eColor))
		.AddDepthStencilAttachment(gDepthStencilMap->GetImageView(vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eDepth));
	return info;
}

void GeometryPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{}

void GeometryPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{

	auto gPosition = ctx.GetOutput(0);
	auto gNormal = ctx.GetOutput(1);
	auto gDepthStencil = ctx.GetFrameLocal(0);

	auto gAlbedoOpacity = ctx.GetOutput(2);
	auto gMetallicRoughness = ctx.GetOutput(3);
	auto gMotionVector = ctx.GetOutput(4);
	auto gEmission = ctx.GetOutput(5);

	auto gPrevPosition = ctx.GetOutput(6);
	auto gPrevNormal = ctx.GetOutput(7);
	auto gPrevDepthStencil = ctx.GetOutput(8);

	auto gPositionHistory = ctx.GetPersitent(0);
	auto gNormalHistory = ctx.GetPersitent(1);
	auto gDepthStencilHistory = ctx.GetPersitent(2);

	auto cmd = cmdCtx.GetCmd();

	gPosition->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	gNormal->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	gDepthStencil->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	gAlbedoOpacity->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	gMetallicRoughness->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	gMotionVector->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	gEmission->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	if (cmd->IsRecording())
		cmd->SubmitToQueue();

	DynamicRenderInfo renderInfo = GenerateDynamicRenderInfo(
		state,
		gPosition, gNormal, gAlbedoOpacity, gMetallicRoughness, gMotionVector, gEmission, gDepthStencil
	);
	DynamicViewport viewPort(state.framebuffer.width, state.framebuffer.height);

	RenderSceneGeometryPassStatic(registry, cmd, state, renderInfo, viewPort);

	Texture2D::CopyTextureAsync(cmd, gPositionHistory, gPrevPosition);
	Texture2D::CopyTextureAsync(cmd, gNormalHistory, gPrevNormal);
	Texture2D::CopyTextureAsync(cmd, gDepthStencilHistory, gPrevDepthStencil);

	Texture2D::CopyTextureAsync(cmd, gPosition, gPositionHistory);
	Texture2D::CopyTextureAsync(cmd, gNormal, gNormalHistory);
	Texture2D::CopyTextureAsync(cmd, gDepthStencil, gDepthStencilHistory);

	if (cmd->IsRecording())
		cmd->SubmitToQueue();
}

void GeometryPass::FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{}

bool GeometryPass::SetupStaticBufferData(
	const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
	GraphicsBindingRecord& binding,
	std::vector<VKRenderObjectData::SceneStaticRenderData::MeshItem>& items,
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

	std::vector<GeometryRenderData> renderData;
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

		using SharedLockType = IndirectDrawManager::SharedLockType;

		indirectManager->WithSharedLock(
			{ SharedLockType::Mesh, SharedLockType::Material },
			[&]()
			{
				std::for_each(std::execution::par_unseq, indices.begin(), indices.end(),
					[&](const size_t& meshIndex)-> void
					{
						size_t inedx = &meshIndex - indices.data();

						auto& item = items[meshIndex];
						auto& material = item.meshinfo.material;
						auto& mesh = item.meshinfo.mesh;

						IndirectDrawCommand& command = commands[inedx];
						GeometryRenderData& data = renderData[startInedx + inedx];

						data.curTransform = item.transform;
						data.prevTransform = item.prevTransform;
						data.motionVectorEnabled = 1;

						uint64_t materialIndex;
						if (indirectManager->GetMaterialIndex_LockFree(*material, materialIndex))
							data.materialIndex = materialIndex;

						IndirectDrawMeta meta;
						if (!indirectManager->GetIndirectDrawMeta_LockFree(*mesh, meta))
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
			});

		startInedx += indices.size();
	}

	renderdata_ssbo->WriteDataAsync(cmd, renderData.data(), renderData.size() * sizeof(GeometryRenderData));
	renderdata_ssbo->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageRead);

	return true;
}

void GeometryPass::RenderSceneGeometryPassStatic(RenderGraph::FrameDataRegistry& registry, const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state, DynamicRenderInfo& renderInfo, DynamicViewport& viewPort)
{

	auto& opaqueMeshes = state.objects.sceneStaticRenderData.opaqueItems;
	auto& renderIndex = state.objects.sceneStaticRenderData.opaqueItems_cullRenderIndex;
	auto& indirectCommands = state.indirectCommands;
	bool hasStaticMeshes = !opaqueMeshes.empty() || !renderIndex.oneSideIndex.empty() || !renderIndex.twoSideIndex.empty();
	bool hasSkinnedMeshes = !indirectCommands.skinnedMesh_OneSideCommand.empty() || !indirectCommands.skinnedMesh_TwoSideCommand.empty();
	if (!hasStaticMeshes && (!hasSkinnedMeshes || !indirectCommands.skinnedMesh_VertexBuffer))
		return;

	auto& shader = _staticShader;
	GraphicsBindingRecord binding;
	std::vector<IndirectDrawCommand> oneSideCommands;
	std::vector<IndirectDrawCommand> twoSideCommands;
	if (hasStaticMeshes)
	{
		auto renderdata_ssbo = registry.GetStorageBlock("renderdata_ssbo");
		binding.SetStorageBlock(renderdata_ssbo, 2);
		if (!SetupStaticBufferData(cmd, binding, opaqueMeshes, renderIndex, oneSideCommands, twoSideCommands))
			return;
	}

	binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);

	auto oneSideCommandBuffer = registry.GetIndirectBlock("oneSideCommandBuffer");
	auto twoSideCommandBuffer = registry.GetIndirectBlock("twoSideCommandBuffer");

	if (!oneSideCommands.empty())
		oneSideCommandBuffer->WriteDataAsync(cmd, oneSideCommands.data(), oneSideCommands.size() * sizeof(IndirectDrawCommand));
	if (!twoSideCommands.empty())
		twoSideCommandBuffer->WriteDataAsync(cmd, twoSideCommands.data(), twoSideCommands.size() * sizeof(IndirectDrawCommand));

	auto skinnedOneSideCommandBuffer = registry.GetIndirectBlock("skinnedOneSideGeometryCommandBuffer");
	auto skinnedTwoSideCommandBuffer = registry.GetIndirectBlock("skinnedTwoSideGeometryCommandBuffer");
	if (hasSkinnedMeshes)
	{
		if (!indirectCommands.skinnedMesh_OneSideCommand.empty())
			skinnedOneSideCommandBuffer->WriteDataAsync(cmd, indirectCommands.skinnedMesh_OneSideCommand.data(), indirectCommands.skinnedMesh_OneSideCommand.size() * sizeof(IndirectDrawCommand));
		if (!indirectCommands.skinnedMesh_TwoSideCommand.empty())
			skinnedTwoSideCommandBuffer->WriteDataAsync(cmd, indirectCommands.skinnedMesh_TwoSideCommand.data(), indirectCommands.skinnedMesh_TwoSideCommand.size() * sizeof(IndirectDrawCommand));
	}

	cmd->setDynamicViewport(viewPort);
	cmd->beginRendering(renderInfo);

	auto indirectManager = IndirectDrawManager::Instance();

	if (!oneSideCommands.empty())
	{
		shader->Bind(cmd, binding);
		oneSideCommandBuffer->Barrier(cmd, BufferUsage::TransferWrite);
		cmd->setCullMode(vk::CullModeFlagBits::eBack);
		cmd->bindVertexBuffers(indirectManager->GetVertexBlock());
		cmd->bindIndexBuffer(indirectManager->GetIndexBlock());
		cmd->drawIndexedIndirect(oneSideCommandBuffer, oneSideCommands.size());
	}

	if (!twoSideCommands.empty())
	{
		if (oneSideCommands.empty())
			shader->Bind(cmd, binding);
		twoSideCommandBuffer->Barrier(cmd, BufferUsage::TransferWrite);
		cmd->setCullMode(vk::CullModeFlagBits::eNone);
		cmd->bindVertexBuffers(indirectManager->GetVertexBlock());
		cmd->bindIndexBuffer(indirectManager->GetIndexBlock());
		cmd->drawIndexedIndirect(twoSideCommandBuffer, twoSideCommands.size());
	}

	if (hasSkinnedMeshes)
	{
		GraphicsBindingRecord skinnedBinding;
		skinnedBinding.SetStorageBlock(indirectCommands.ssbo_SkinnedMesh_GeometryRenderData, 2);
		auto indirectManager = IndirectDrawManager::Instance();
		skinnedBinding.SetBindlessMaterialTexture(indirectManager->GetMaterialSSBO(), BindlessTextureManager::Instance());
		skinnedBinding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
		shader->Bind(cmd, skinnedBinding);

		if (!indirectCommands.skinnedMesh_OneSideCommand.empty())
		{
			skinnedOneSideCommandBuffer->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::IndirectRead);
			cmd->setCullMode(vk::CullModeFlagBits::eBack);
			cmd->bindVertexBuffers(indirectCommands.skinnedMesh_VertexBuffer);
			cmd->bindIndexBuffer(indirectManager->GetIndexBlock());
			cmd->drawIndexedIndirect(skinnedOneSideCommandBuffer, indirectCommands.skinnedMesh_OneSideCommand.size());
		}

		if (!indirectCommands.skinnedMesh_TwoSideCommand.empty())
		{
			skinnedTwoSideCommandBuffer->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::IndirectRead);
			cmd->setCullMode(vk::CullModeFlagBits::eNone);
			cmd->bindVertexBuffers(indirectCommands.skinnedMesh_VertexBuffer);
			cmd->bindIndexBuffer(indirectManager->GetIndexBlock());
			cmd->drawIndexedIndirect(skinnedTwoSideCommandBuffer, indirectCommands.skinnedMesh_TwoSideCommand.size());
		}
	}

	cmd->endRendering();
}

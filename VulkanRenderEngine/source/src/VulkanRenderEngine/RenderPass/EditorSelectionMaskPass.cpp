#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/EditorSelectionMaskPass.h"
#include "VulkanRenderEngine/RenderPass/GeometryPass.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"
#include "VulkanRenderEngine/General/RenderHelper.h"
#include "VulkanRenderEngine/General/EditorViewport.h"

#include <algorithm>
#include <vector>

namespace
{
	// 和 selectionMask.fs 的 push_constant 一致
	struct alignas(16) MaskParams
	{
		uint32_t primaryId = 0;			// 主选中的 entityId + 1（0 = 不区分）
		float opacityThreshold = 0.01f;	// 材质透明度低于它的像素不算覆盖（不透明 0.01，同 GeometryPass；透明 = GeometryPass::TransparentPickOpacity）
		uint32_t pad[2] = {};
	};
	static_assert(sizeof(MaskParams) == 16);

	constexpr float kOpaqueOpacityThreshold = 0.01f;

	// 遮罩各通道取 max：重叠时 r / g / b 都取并集（同一像素只要有一个选中物体看得见就算看得见）
	vk::PipelineColorBlendAttachmentState MaxBlend()
	{
		return vk::PipelineColorBlendAttachmentState()
			.setBlendEnable(vk::True)
			.setColorBlendOp(vk::BlendOp::eMax)
			.setAlphaBlendOp(vk::BlendOp::eMax)
			.setSrcColorBlendFactor(vk::BlendFactor::eOne)
			.setDstColorBlendFactor(vk::BlendFactor::eOne)
			.setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
			.setDstAlphaBlendFactor(vk::BlendFactor::eOne)
			.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
	}

	std::shared_ptr<GraphicsPipeline> CreateMaskPipeline(const std::string& vs, const std::string& fs, bool skinned)
	{
		auto pipeline = std::make_shared<GraphicsPipeline>();

		GraphicsPipelineConfig config;
		config.vertexPath = vs;
		config.fragmentPath = fs;
		config.AddVertexInputAttributeDescription(StaticMesh::GetVertexInputAttributeDescription());
		config.AddVertexInputBindingDescription(StaticMesh::GetVertexInputBindingDescription());
		config.AddDefineMacro("EDITOR_ENTITY_ID");		// 顶点着色器输出 EntityId（location 9）

		config.AddColorAttachment(EditorSelectionMaskPass::GetMaskFormat(), MaxBlend());
		config.SetDepthTest(false, false);				// 没有深度附件，画完整轮廓

		config
			.AddBindlessMaterialTextureBinding()		// 按材质透明度 discard
			.AddCameraUnifromDataBinding()
			.AddStorageBuffer(2);						// GeometryRenderData
		if (skinned)
			config.AddStorageBuffer(3).AddStorageBuffer(4);	// 蒙皮 motion 数据、上一帧蒙皮顶点（VS 声明了就要绑）
		config.AddUnifromTexture(5);					// 实体 ID 缓冲
		config.AddPushConstant(sizeof(MaskParams));
		config.AddDynamicState(vk::DynamicState::eCullMode);

		if (config.Validate())
			pipeline->Create(config);
		return pipeline;
	}

	using StaticItems = std::vector<VKRenderObjectData::SceneStaticRenderData::MeshItem>;
	using SkinnedItems = std::vector<VKRenderObjectData::SceneSkinnedRenderData::SkinnedMeshItem>;

	// 静态网格：挑出选中的条目，追加到 data / commands（firstInstance 指向 data 里的下标）
	void CollectStatic(const StaticItems& items, const EditorSelection& selection, IndirectDrawManager& indirectManager,
		std::vector<GeometryRenderData>& data, std::vector<IndirectDrawCommand>& commands)
	{
		for (const auto& item : items)
		{
			if (item.entityId == UINT32_MAX || !selection.Contains(item.entityId))
				continue;
			const auto& mesh = item.meshinfo.mesh;
			const auto& material = item.meshinfo.material;
			if (!mesh || !material)
				continue;

			IndirectDrawMeta meta;
			if (!indirectManager.GetIndirectDrawMeta_LockFree(*mesh, meta))
				continue;

			GeometryRenderData renderData;
			renderData.curTransform = item.transform;
			renderData.prevTransform = item.prevTransform;
			renderData.entityId = item.entityId;
			uint64_t materialIndex = 0;
			if (indirectManager.GetMaterialIndex_LockFree(*material, materialIndex))
				renderData.materialIndex = uint32_t(materialIndex);

			IndirectDrawCommand command{};
			command.indexCount = meta.indexCount;
			command.firstIndex = meta.firstIndex;
			command.vertexOffset = meta.vertexOffset;
			command.instanceCount = 1;
			command.firstInstance = uint32_t(data.size());

			data.push_back(renderData);
			commands.push_back(command);
		}
	}

	// 蒙皮网格：SkinnedPass 生成的第 i 条命令对应第 i 个条目，只挑出选中的命令重放
	void CollectSkinned(const SkinnedItems& items, const std::vector<IndirectDrawCommand>& source, const EditorSelection& selection,
		std::vector<IndirectDrawCommand>& commands)
	{
		const size_t count = std::min(items.size(), source.size());
		for (size_t i = 0; i < count; ++i)
		{
			if (source[i].instanceCount == 0 || items[i].entityId == UINT32_MAX || !selection.Contains(items[i].entityId))
				continue;
			commands.push_back(source[i]);
		}
	}
}

EditorSelectionMaskPass::EditorSelectionMaskPass(const std::string& fragmentShaderPath, const std::string& staticVertexShaderPath, const std::string& skinnedVertexShaderPath)
{
	_staticShader = CreateMaskPipeline(staticVertexShaderPath, fragmentShaderPath, false);
	_skinnedShader = CreateMaskPipeline(skinnedVertexShaderPath, fragmentShaderPath, true);
}

bool EditorSelectionMaskPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	const auto* frame = state.editorSelection.get();
	return frame && frame->selection && !frame->selection->Empty();
}

void EditorSelectionMaskPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	auto* frame = state.editorSelection.get();
	if (!frame || !frame->selection || frame->selection->Empty())
		return;
	const EditorSelection& selection = *frame->selection;

	auto idBuffer = ctx.GetInput(0);
	auto mask = ctx.GetExternal(0);
	if (!idBuffer || idBuffer->IsEmpty() || !mask || mask->IsEmpty()
		|| mask->GetWidth() != state.framebuffer.width || mask->GetHeight() != state.framebuffer.height)
		return;

	auto indirectManager = IndirectDrawManager::Instance();
	auto materialSSBO = indirectManager->GetMaterialSSBO();
	if (!materialSSBO)
		return;

	// 透明物体只在画出来时才描边（和拾取一致：GeometryPass 也只在 drawTransparent 时写透明物体的 ID）
	const bool withTransparent = state.option.flags.drawTransparent;

	// ── 静态网格：不透明在前、透明在后，共用一份 GeometryRenderData，两段命令分开画（透明度阈值不同）。
	//    不用剔除后的列表：被剔除的就是看不见，画不画都一样；选中的一般只有几个 ──
	std::vector<GeometryRenderData> staticData;
	std::vector<IndirectDrawCommand> staticCommands;
	size_t staticOpaqueCount = 0;
	{
		using SharedLockType = IndirectDrawManager::SharedLockType;
		indirectManager->WithSharedLock({ SharedLockType::Mesh, SharedLockType::Material }, [&]()
			{
				CollectStatic(state.objects.sceneStaticRenderData.opaqueItems, selection, *indirectManager, staticData, staticCommands);
				staticOpaqueCount = staticCommands.size();
				if (withTransparent)
					CollectStatic(state.objects.sceneStaticRenderData.transparentItems, selection, *indirectManager, staticData, staticCommands);
			});
	}
	const size_t staticTransparentCount = staticCommands.size() - staticOpaqueCount;

	// ── 蒙皮网格：同样不透明在前（单面、双面）、透明在后 ──
	std::vector<IndirectDrawCommand> skinnedCommands;
	size_t skinnedOpaqueCount = 0;
	auto& indirect = state.indirectCommands;
	const auto& skinnedVertices = state.curFrameSkinnedData ? state.curFrameSkinnedData->skinnedMesh_VertexBuffer : nullptr;
	const bool skinnedReady = skinnedVertices && skinnedVertices->buffer && indirect.ssbo_SkinnedMesh_GeometryRenderData
		&& indirect.ssbo_SkinnedMesh_MotionData && _skinnedShader && _skinnedShader->IsValid();
	if (skinnedReady)
	{
		const auto& skinned = state.objects.sceneSkinnedRenderData;
		CollectSkinned(skinned.opaqueOneSideItems, indirect.skinnedMesh_OneSideCommand, selection, skinnedCommands);
		CollectSkinned(skinned.opaqueTwoSideItems, indirect.skinnedMesh_TwoSideCommand, selection, skinnedCommands);
		skinnedOpaqueCount = skinnedCommands.size();
		if (withTransparent)
			CollectSkinned(skinned.transparentItems, indirect.transparentMesh_Command, selection, skinnedCommands);
	}
	const size_t skinnedTransparentCount = skinnedCommands.size() - skinnedOpaqueCount;

	const bool drawStatic = !staticCommands.empty() && _staticShader && _staticShader->IsValid();
	const bool drawSkinned = !skinnedCommands.empty();
	if (!drawStatic && !drawSkinned)
		return;		// 选中的都不是网格（灯光等）或者不可见：不画，描边跳过

	auto cmd = cmdCtx.GetCmd();
	if (!cmd)
		return;

	idBuffer->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Read);
	mask->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);

	// 本帧的上传数据放在 registry 里（每帧一份，和 GeometryPass 的 renderdata_ssbo 一样）
	std::shared_ptr<StorageBlock> staticDataBlock;
	std::shared_ptr<IndirectBufferBlock> staticCommandBlock;
	if (drawStatic)
	{
		staticDataBlock = registry.GetStorageBlock("editorSelectionMask_renderData");
		staticCommandBlock = registry.GetIndirectBlock("editorSelectionMask_staticCommands");
		staticDataBlock->WriteDataAsync(cmd, staticData.data(), staticData.size() * sizeof(GeometryRenderData));
		staticDataBlock->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageRead);
		staticCommandBlock->WriteDataAsync(cmd, staticCommands.data(), staticCommands.size() * sizeof(IndirectDrawCommand));
		staticCommandBlock->Barrier(cmd, BufferUsage::TransferWrite);
	}
	std::shared_ptr<IndirectBufferBlock> skinnedCommandBlock;
	if (drawSkinned)
	{
		skinnedCommandBlock = registry.GetIndirectBlock("editorSelectionMask_skinnedCommands");
		skinnedCommandBlock->WriteDataAsync(cmd, skinnedCommands.data(), skinnedCommands.size() * sizeof(IndirectDrawCommand));
		skinnedCommandBlock->Barrier(cmd, BufferUsage::TransferWrite);
	}

	MaskParams params;
	params.primaryId = selection.primary == UINT32_MAX ? 0u : selection.primary + 1u;

	// 一段命令（[first, first + count)）用指定的透明度阈值画
	auto drawRange = [&](GraphicsPipeline& shader, const std::shared_ptr<IndirectBufferBlock>& commands, size_t first, size_t count, float threshold)
		{
			if (count == 0)
				return;
			params.opacityThreshold = threshold;
			shader.SetPushConstants(cmd, &params, sizeof(params));
			cmd->drawIndexedIndirect(commands, uint32_t(count), sizeof(IndirectDrawCommand), vk::DeviceSize(first * sizeof(IndirectDrawCommand)));
		};

	DynamicRenderInfo info;
	info.SetRenderArea(state.framebuffer.width, state.framebuffer.height)
		.AddColorAttachment(mask->GetImageView(vk::ImageAspectFlagBits::eColor));		// 清成 0 = 没有选中物体
	DynamicViewport viewport(state.framebuffer.width, state.framebuffer.height);

	cmd->setDynamicViewport(viewport);
	cmd->beginRendering(info);

	if (drawStatic)
	{
		GraphicsBindingRecord binding;
		binding.SetBindlessMaterialTexture(materialSSBO, BindlessTextureManager::Instance());
		binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
		binding.SetStorageBlock(staticDataBlock, 2);
		binding.SetUniformTexture(idBuffer, vk::ImageAspectFlagBits::eColor, 5);

		_staticShader->SetPushConstants(cmd, &params, sizeof(params));
		_staticShader->Bind(cmd, binding);
		cmd->setCullMode(vk::CullModeFlagBits::eNone);		// 轮廓要完整：背面也画
		cmd->bindVertexBuffers(indirectManager->GetVertexBlock());
		cmd->bindIndexBuffer(indirectManager->GetIndexBlock());
		drawRange(*_staticShader, staticCommandBlock, 0, staticOpaqueCount, kOpaqueOpacityThreshold);
		drawRange(*_staticShader, staticCommandBlock, staticOpaqueCount, staticTransparentCount, GeometryPass::TransparentPickOpacity);
	}

	if (drawSkinned)
	{
		std::shared_ptr<StorageBlock> prevSkinnedVertices;
		if (state.preFrameSkinnedData && state.preFrameSkinnedData->skinnedMesh_VertexBuffer
			&& state.preFrameSkinnedData->skinnedMesh_VertexBuffer->buffer)
			prevSkinnedVertices = state.preFrameSkinnedData->skinnedMesh_VertexBuffer->buffer;
		else
		{
			if (!_dummySkinnedVertexBuffer)
				_dummySkinnedVertexBuffer = std::make_shared<VertexBufferBlock>(sizeof(Vertex));
			prevSkinnedVertices = _dummySkinnedVertexBuffer;
		}

		GraphicsBindingRecord binding;
		binding.SetBindlessMaterialTexture(materialSSBO, BindlessTextureManager::Instance());
		binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
		binding.SetStorageBlock(indirect.ssbo_SkinnedMesh_GeometryRenderData, 2);
		binding.SetStorageBlock(indirect.ssbo_SkinnedMesh_MotionData, 3);
		binding.SetStorageBlock(prevSkinnedVertices, 4);
		binding.SetUniformTexture(idBuffer, vk::ImageAspectFlagBits::eColor, 5);

		_skinnedShader->SetPushConstants(cmd, &params, sizeof(params));
		_skinnedShader->Bind(cmd, binding);
		cmd->setCullMode(vk::CullModeFlagBits::eNone);
		cmd->bindVertexBuffers(skinnedVertices->buffer);
		cmd->bindIndexBuffer(indirectManager->GetIndexBlock());
		drawRange(*_skinnedShader, skinnedCommandBlock, 0, skinnedOpaqueCount, kOpaqueOpacityThreshold);
		drawRange(*_skinnedShader, skinnedCommandBlock, skinnedOpaqueCount, skinnedTransparentCount, GeometryPass::TransparentPickOpacity);
	}

	cmd->endRendering();
	cmd->SubmitToQueue();

	frame->maskDrawn.store(true, std::memory_order_release);
}

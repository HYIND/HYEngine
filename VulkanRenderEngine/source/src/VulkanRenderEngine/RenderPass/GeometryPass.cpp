#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/GeometryPass.h"
#include "VulkanRenderEngine/General/RenderHelper.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"
#include "VulkanRenderEngine/GlobalConfig.h"

#include <iostream>

namespace
{
	// optimal tiling 下同时支持颜色附件、采样、storage（见 GeometryPass.h 的说明）才用 preferred，否则回退
	vk::Format PickGBufferFormat(vk::Format preferred, vk::Format fallback)
	{
		const auto physicalDevice = VKCONTEXT->GetDevice()->GetPhysicalDevice();
		const vk::FormatFeatureFlags features = physicalDevice.getFormatProperties(preferred).optimalTilingFeatures;
		const vk::FormatFeatureFlags required =
			vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eStorageImage;
		return (features & required) == required ? preferred : fallback;
	}

	// 编辑器的实体 ID 附件：整数格式不能混合，只写 R
	void AddEntityIdAttachment(GraphicsPipelineConfig& config)
	{
		const auto state = vk::PipelineColorBlendAttachmentState()
			.setBlendEnable(vk::False)
			.setColorWriteMask(vk::ColorComponentFlagBits::eR);
		config.AddColorAttachment(GeometryPass::GetEntityIdFormat(), state);
		config.AddDefineMacro("EDITOR_ENTITY_ID");
	}

	constexpr const char* kTransparentIdFragmentShader = "shader/editor/transparentId.fs";

	// 透明物体写实体 ID：只有 R32_UINT 一个附件；深度测试 + 写入（临时深度，不碰 gDepthStencil）
	std::shared_ptr<GraphicsPipeline> CreateTransparentIdPipeline(const std::string& vertexPath, bool skinned)
	{
		auto pipeline = std::make_shared<GraphicsPipeline>();

		GraphicsPipelineConfig config;
		config.vertexPath = vertexPath;
		config.fragmentPath = kTransparentIdFragmentShader;
		config.AddVertexInputAttributeDescription(StaticMesh::GetVertexInputAttributeDescription());
		config.AddVertexInputBindingDescription(StaticMesh::GetVertexInputBindingDescription());
		config.AddDefineMacro("EDITOR_ENTITY_ID");
		config.AddDefineMacro("EDITOR_TRANSPARENT_PICK_OPACITY", GeometryPass::TransparentPickOpacity);

		const auto state = vk::PipelineColorBlendAttachmentState()
			.setBlendEnable(vk::False)
			.setColorWriteMask(vk::ColorComponentFlagBits::eR);
		config.AddColorAttachment(GeometryPass::GetEntityIdFormat(), state);
		config.SetDepthStencilAttachmentFormat(vk::Format::eD24UnormS8Uint);
		config.SetDepthTest(true, true, vk::CompareOp::eLess);

		config
			.AddBindlessMaterialTextureBinding()
			.AddCameraUnifromDataBinding()
			.AddStorageBuffer(2);
		if (skinned)
			config.AddStorageBuffer(3).AddStorageBuffer(4);		// 蒙皮 VS 声明了 motion 数据、上一帧顶点，就要绑
		config.AddDynamicState(vk::DynamicState::eCullMode);

		if (config.Validate())
			pipeline->Create(config);
		return pipeline;
	}
}

bool GeometryPass::HasEntityIdOutput()
{
	return GlobalConfig::EditorViewportFeatures;
}

vk::Format GeometryPass::GetMotionVectorFormat()
{
	static const vk::Format format = PickGBufferFormat(vk::Format::eR16G16Sfloat, vk::Format::eR16G16B16A16Sfloat);
	return format;
}

vk::Format GeometryPass::GetEmissionFormat()
{
	static const vk::Format format = PickGBufferFormat(vk::Format::eB10G11R11UfloatPack32, vk::Format::eR16G16B16A16Sfloat);
	return format;
}

GeometryPass::GeometryPass(
	const std::string& staticMeshVertexShaderPath,
	const std::string& staticMeshFragmentShaderPath,
	const std::string& skinnedMeshVertexShaderPath
)
{
	_hasEntityId = HasEntityIdOutput();

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
		config.AddColorAttachment(GetMotionVectorFormat());		// gMotionVector
		config.AddColorAttachment(GetEmissionFormat());			// gEmission
		if (_hasEntityId)
			AddEntityIdAttachment(config);						// gEntityId（编辑器）

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
		config.vertexPath = skinnedMeshVertexShaderPath;
		config.fragmentPath = staticMeshFragmentShaderPath;

		config.AddVertexInputAttributeDescription(StaticMesh::GetVertexInputAttributeDescription());
		config.AddVertexInputBindingDescription(StaticMesh::GetVertexInputBindingDescription());

		config.AddColorAttachment(vk::Format::eR32G32B32A32Sfloat);
		config.AddColorAttachment(vk::Format::eR16G16B16A16Sfloat);
		config.AddColorAttachment(vk::Format::eR8G8B8A8Unorm);
		config.AddColorAttachment(vk::Format::eR16G16B16A16Sfloat);
		config.AddColorAttachment(GetMotionVectorFormat());		// gMotionVector
		config.AddColorAttachment(GetEmissionFormat());			// gEmission
		if (_hasEntityId)
			AddEntityIdAttachment(config);						// gEntityId（编辑器）

		config.SetDepthStencilAttachmentFormat(vk::Format::eD24UnormS8Uint);

		config
			.AddBindlessMaterialTextureBinding()
			.AddCameraUnifromDataBinding()
			.AddStorageBuffer(2)
			.AddStorageBuffer(3)
			.AddStorageBuffer(4);

		config.AddDynamicState(vk::DynamicState::eCullMode);

		if (config.Validate())
			_skinnedShader->Create(config);
	}

	// 编辑器：透明物体写实体 ID（顶点着色器和 G-Buffer 用同一套）
	if (_hasEntityId)
	{
		_transparentIdStaticShader = CreateTransparentIdPipeline(staticMeshVertexShaderPath, false);
		_transparentIdSkinnedShader = CreateTransparentIdPipeline(skinnedMeshVertexShaderPath, true);
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
	std::shared_ptr<Texture2D>& gDepthStencilMap,
	const std::shared_ptr<Texture2D>& gEntityId
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
		.AddColorAttachment(gEmission->GetImageView(vk::ImageAspectFlagBits::eColor));
	// 清屏值全 0：按 uint 解释就是 0 = 没有物体
	if (gEntityId)
		info.AddColorAttachment(gEntityId->GetImageView(vk::ImageAspectFlagBits::eColor));
	info.AddDepthStencilAttachment(gDepthStencilMap->GetImageView(vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eDepth));
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

	// 管线带了实体 ID 附件，渲染图就必须给第 7 个输出（见 VKRenderer3D::InitSceneRenderGraph），否则附件数对不上，不能画
	std::shared_ptr<Texture2D> gEntityId;
	if (_hasEntityId)
	{
		gEntityId = ctx.GetOutput(6);
		if (!gEntityId || gEntityId->IsEmpty())
		{
			std::cout << "[ GeometryPass ] 管线带实体 ID 附件，但渲染图没有给 gEntityId 输出，跳过本帧\n";
			return;
		}
	}

	auto cmd = cmdCtx.GetCmd();

	gPosition->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	gNormal->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	gDepthStencil->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	gAlbedoOpacity->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	gMetallicRoughness->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	gMotionVector->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	gEmission->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	if (gEntityId)
		gEntityId->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	if (cmd->IsRecording())
		cmd->SubmitToQueue();

	DynamicRenderInfo renderInfo = GenerateDynamicRenderInfo(
		state,
		gPosition, gNormal, gAlbedoOpacity, gMetallicRoughness, gMotionVector, gEmission, gDepthStencil, gEntityId
	);
	DynamicViewport viewPort(state.framebuffer.width, state.framebuffer.height);

	RenderSceneGeometryPassStatic(registry, cmd, state, renderInfo, viewPort);

	// 编辑器：透明物体也写进实体 ID 缓冲（透明物体不画时不写，和画面一致）
	if (gEntityId && state.option.flags.drawTransparent)
		RenderTransparentEntityIds(registry, cmd, state, gEntityId, gDepthStencil, ctx.GetTemp(0));

	if (cmd->IsRecording())
		cmd->SubmitToQueue();
}

void GeometryPass::RenderTransparentEntityIds(RenderGraph::FrameDataRegistry& registry, const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state,
	const std::shared_ptr<Texture2D>& gEntityId, const std::shared_ptr<Texture2D>& gDepthStencil, const std::shared_ptr<Texture2D>& tempDepth)
{
	if (!cmd || !gEntityId || !gDepthStencil || !tempDepth || tempDepth->IsEmpty()
		|| tempDepth->GetWidth() != gDepthStencil->GetWidth() || tempDepth->GetHeight() != gDepthStencil->GetHeight())
		return;

	auto indirectManager = IndirectDrawManager::Instance();
	auto materialSSBO = indirectManager->GetMaterialSSBO();
	if (!materialSSBO)
		return;

	// ── 静态透明网格：和 TransparentPass 一样用剔除后的列表（不按远近排序：有深度写入，顺序无所谓） ──
	std::vector<GeometryRenderData> staticData;
	std::vector<IndirectDrawCommand> staticCommands;
	const bool staticReady = _transparentIdStaticShader && _transparentIdStaticShader->IsValid();
	if (staticReady)
	{
		const auto& items = state.objects.sceneStaticRenderData.transparentItems;
		const auto& cullIndex = state.objects.sceneStaticRenderData.transparentItems_cullRenderIndex;
		using SharedLockType = IndirectDrawManager::SharedLockType;
		indirectManager->WithSharedLock({ SharedLockType::Mesh, SharedLockType::Material }, [&]()
			{
				for (const size_t index : cullIndex)
				{
					if (index >= items.size())
						continue;
					const auto& item = items[index];
					if (item.entityId == UINT32_MAX || !item.meshinfo.mesh || !item.meshinfo.material)
						continue;

					IndirectDrawMeta meta;
					if (!indirectManager->GetIndirectDrawMeta_LockFree(*item.meshinfo.mesh, meta))
						continue;

					GeometryRenderData data;
					data.curTransform = item.transform;
					data.prevTransform = item.prevTransform;
					data.entityId = item.entityId;
					uint64_t materialIndex = 0;
					if (indirectManager->GetMaterialIndex_LockFree(*item.meshinfo.material, materialIndex))
						data.materialIndex = uint32_t(materialIndex);

					IndirectDrawCommand command{};
					command.indexCount = meta.indexCount;
					command.firstIndex = meta.firstIndex;
					command.vertexOffset = meta.vertexOffset;
					command.instanceCount = 1;
					command.firstInstance = uint32_t(staticData.size());

					staticData.push_back(data);
					staticCommands.push_back(command);
				}
			});
	}

	// ── 蒙皮透明网格：SkinnedPass 已经生成了命令和带 entityId 的 GeometryRenderData（和 TransparentPass 用的同一份） ──
	auto& indirect = state.indirectCommands;
	const auto& skinnedCommands = indirect.transparentMesh_Command;
	const auto& skinnedVertices = state.curFrameSkinnedData ? state.curFrameSkinnedData->skinnedMesh_VertexBuffer : nullptr;
	const bool drawSkinned = !skinnedCommands.empty() && skinnedVertices && skinnedVertices->buffer
		&& indirect.ssbo_SkinnedMesh_GeometryRenderData && indirect.ssbo_SkinnedMesh_MotionData
		&& _transparentIdSkinnedShader && _transparentIdSkinnedShader->IsValid();
	const bool drawStatic = !staticCommands.empty();
	if (!drawStatic && !drawSkinned)
		return;

	// 不透明深度拷一份：透明物体在拷贝上做深度测试并写入（只留最近的透明面），gDepthStencil 保持不透明深度给后面的 Pass 用
	if (!Texture2D::CopyTextureAsync(cmd, gDepthStencil, tempDepth))
		return;
	tempDepth->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	gDepthStencil->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	gEntityId->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);

	std::shared_ptr<StorageBlock> staticDataBlock;
	std::shared_ptr<IndirectBufferBlock> staticCommandBlock;
	if (drawStatic)
	{
		staticDataBlock = registry.GetStorageBlock("geometry_transparentId_renderData");
		staticCommandBlock = registry.GetIndirectBlock("geometry_transparentId_staticCommands");
		staticDataBlock->WriteDataAsync(cmd, staticData.data(), staticData.size() * sizeof(GeometryRenderData));
		staticDataBlock->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageRead);
		staticCommandBlock->WriteDataAsync(cmd, staticCommands.data(), staticCommands.size() * sizeof(IndirectDrawCommand));
		staticCommandBlock->Barrier(cmd, BufferUsage::TransferWrite);
	}
	std::shared_ptr<IndirectBufferBlock> skinnedCommandBlock;
	if (drawSkinned)
	{
		skinnedCommandBlock = registry.GetIndirectBlock("geometry_transparentId_skinnedCommands");
		skinnedCommandBlock->WriteDataAsync(cmd, skinnedCommands.data(), skinnedCommands.size() * sizeof(IndirectDrawCommand));
		skinnedCommandBlock->Barrier(cmd, BufferUsage::TransferWrite);
	}

	DynamicRenderInfo info;
	info.SetRenderArea(state.framebuffer.width, state.framebuffer.height)
		.AddColorAttachment(gEntityId->GetImageView(vk::ImageAspectFlagBits::eColor), vk::AttachmentLoadOp::eLoad)
		.AddDepthStencilAttachment(tempDepth->GetImageView(vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil), vk::AttachmentLoadOp::eLoad);
	DynamicViewport viewport(state.framebuffer.width, state.framebuffer.height);

	cmd->setDynamicViewport(viewport);
	cmd->beginRendering(info);

	if (drawStatic)
	{
		GraphicsBindingRecord binding;
		binding.SetBindlessMaterialTexture(materialSSBO, BindlessTextureManager::Instance());
		binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
		binding.SetStorageBlock(staticDataBlock, 2);
		_transparentIdStaticShader->Bind(cmd, binding);
		cmd->setCullMode(vk::CullModeFlagBits::eBack);		// 和 TransparentPass 一样只画正面
		cmd->bindVertexBuffers(indirectManager->GetVertexBlock());
		cmd->bindIndexBuffer(indirectManager->GetIndexBlock());
		cmd->drawIndexedIndirect(staticCommandBlock, uint32_t(staticCommands.size()));
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
		_transparentIdSkinnedShader->Bind(cmd, binding);
		cmd->setCullMode(vk::CullModeFlagBits::eBack);
		cmd->bindVertexBuffers(skinnedVertices->buffer);
		cmd->bindIndexBuffer(indirectManager->GetIndexBlock());
		cmd->drawIndexedIndirect(skinnedCommandBlock, uint32_t(skinnedCommands.size()));
	}

	cmd->endRendering();
}

void GeometryPass::FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{}

void GBufferHistoryCopyPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	auto gPosition = ctx.GetInput(0);
	auto gNormal = ctx.GetInput(1);
	auto gDepthStencil = ctx.GetFrameLocal(0);

	auto gPositionHistory = ctx.GetPersitent(0);
	auto gNormalHistory = ctx.GetPersitent(1);
	auto gDepthStencilHistory = ctx.GetPersitent(2);

	auto cmd = cmdCtx.GetCmd();
	if (!cmd)
		return;

	// CopyTextureAsync 自己把源 / 目标切到 TransferSrc / TransferDst；下一帧读历史的 Pass 按各自声明的布局再切回去
	Texture2D::CopyTextureAsync(cmd, gPosition, gPositionHistory);
	Texture2D::CopyTextureAsync(cmd, gNormal, gNormalHistory);
	Texture2D::CopyTextureAsync(cmd, gDepthStencil, gDepthStencilHistory);

	if (cmd->IsRecording())
		cmd->SubmitToQueue();
}

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
						data.entityId = item.entityId;

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

	// 没有可绘制的网格时也要走一遍 beginRendering：GBuffer / 深度只靠 loadOp = eClear 清空，
	// 直接返回的话它们保留上一帧（或纹理池里别的资源）的内容，光照 / SSAO / 天空 / 间接光都会读到残影
	auto clearOnly = [&]() {
		cmd->setDynamicViewport(viewPort);
		cmd->beginRendering(renderInfo);
		cmd->endRendering();
		};

	if (!hasStaticMeshes && (!hasSkinnedMeshes
		|| !state.curFrameSkinnedData
		|| !state.curFrameSkinnedData->skinnedMesh_VertexBuffer
		|| !state.curFrameSkinnedData->skinnedMesh_VertexBuffer->buffer))
	{
		clearOnly();
		return;
	}

	auto& shader = _staticShader;
	GraphicsBindingRecord binding;
	std::vector<IndirectDrawCommand> oneSideCommands;
	std::vector<IndirectDrawCommand> twoSideCommands;
	if (hasStaticMeshes)
	{
		auto renderdata_ssbo = registry.GetStorageBlock("renderdata_ssbo");
		binding.SetStorageBlock(renderdata_ssbo, 2);
		if (!SetupStaticBufferData(cmd, binding, opaqueMeshes, renderIndex, oneSideCommands, twoSideCommands))
		{
			clearOnly();
			return;
		}
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
		auto& curSkinnedVertices = state.curFrameSkinnedData->skinnedMesh_VertexBuffer;

		std::shared_ptr<StorageBlock> prevSkinnedVertices;
		if (state.preFrameSkinnedData
			&& state.preFrameSkinnedData->skinnedMesh_VertexBuffer
			&& state.preFrameSkinnedData->skinnedMesh_VertexBuffer->buffer)
			prevSkinnedVertices = state.preFrameSkinnedData->skinnedMesh_VertexBuffer->buffer;
		else
		{
			if (!_dummySkinnedVertexBuffer)
				_dummySkinnedVertexBuffer = std::make_shared<VertexBufferBlock>(sizeof(Vertex));
			prevSkinnedVertices = _dummySkinnedVertexBuffer;
		}

		GraphicsBindingRecord skinnedBinding;
		skinnedBinding.SetStorageBlock(indirectCommands.ssbo_SkinnedMesh_GeometryRenderData, 2);
		skinnedBinding.SetStorageBlock(indirectCommands.ssbo_SkinnedMesh_MotionData, 3);
		skinnedBinding.SetStorageBlock(prevSkinnedVertices, 4);
		prevSkinnedVertices->Barrier(cmd, BufferUsage::VertexAttributeRead, BufferUsage::StorageRead);
		auto indirectManager = IndirectDrawManager::Instance();
		skinnedBinding.SetBindlessMaterialTexture(indirectManager->GetMaterialSSBO(), BindlessTextureManager::Instance());
		skinnedBinding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
		if (_skinnedShader->IsValid())
			_skinnedShader->Bind(cmd, skinnedBinding);
		else
			shader->Bind(cmd, skinnedBinding);

		if (!indirectCommands.skinnedMesh_OneSideCommand.empty())
		{
			skinnedOneSideCommandBuffer->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::IndirectRead);
			cmd->setCullMode(vk::CullModeFlagBits::eBack);
			cmd->bindVertexBuffers(curSkinnedVertices->buffer);
			cmd->bindIndexBuffer(indirectManager->GetIndexBlock());
			cmd->drawIndexedIndirect(skinnedOneSideCommandBuffer, indirectCommands.skinnedMesh_OneSideCommand.size());
		}

		if (!indirectCommands.skinnedMesh_TwoSideCommand.empty())
		{
			skinnedTwoSideCommandBuffer->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::IndirectRead);
			cmd->setCullMode(vk::CullModeFlagBits::eNone);
			cmd->bindVertexBuffers(curSkinnedVertices->buffer);
			cmd->bindIndexBuffer(indirectManager->GetIndexBlock());
			cmd->drawIndexedIndirect(skinnedTwoSideCommandBuffer, indirectCommands.skinnedMesh_TwoSideCommand.size());
		}
	}

	cmd->endRendering();
}

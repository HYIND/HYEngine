#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/TransparentPass.h"
#include "VulkanRenderEngine/General/RenderHelper.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"
#include "VulkanRenderEngine/Base/Material.h"

#include <algorithm>
#include <execution>
#include <fstream>

using namespace VKRenderObjectData;

namespace
{
	// OIT 累积缓冲
	vk::PipelineColorBlendAttachmentState MakeOitAccumBlend()
	{
		vk::PipelineColorBlendAttachmentState blend;
		blend
			.setBlendEnable(true)
			.setColorBlendOp(vk::BlendOp::eAdd)
			.setAlphaBlendOp(vk::BlendOp::eAdd)
			.setSrcColorBlendFactor(vk::BlendFactor::eOne)
			.setDstColorBlendFactor(vk::BlendFactor::eOne)
			.setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
			.setDstAlphaBlendFactor(vk::BlendFactor::eOne)
			.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
		return blend;
	}

	// OIT 透射率缓冲
	vk::PipelineColorBlendAttachmentState MakeOitRevealBlend()
	{
		vk::PipelineColorBlendAttachmentState blend;
		blend
			.setBlendEnable(true)
			.setColorBlendOp(vk::BlendOp::eAdd)
			.setAlphaBlendOp(vk::BlendOp::eAdd)
			.setSrcColorBlendFactor(vk::BlendFactor::eZero)
			.setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcColor)
			.setSrcAlphaBlendFactor(vk::BlendFactor::eZero)
			.setDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcColor)
			.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
		return blend;
	}
}

TransparentPass::TransparentPass(
	const std::string& vertexShaderPath,
	const std::string& fragmentShaderPath,
	const std::string& oitVertexShaderPath,
	const std::string& oitFragmentShaderPath,
	const std::string& oitCompositeVertexShaderPath,
	const std::string& oitCompositeFragmentShaderPath
)
{
	// ---------------- Sorted：直接前向着色到场景色 ----------------
	{
		_sortedShader = std::make_shared<GraphicsPipeline>();

		GraphicsPipelineConfig config;
		config.vertexPath = vertexShaderPath;
		config.fragmentPath = fragmentShaderPath;

		config.AddVertexInputAttributeDescription(StaticMesh::GetVertexInputAttributeDescription());
		config.AddVertexInputBindingDescription(StaticMesh::GetVertexInputBindingDescription());

		config.AddColorAttachment(vk::Format::eR16G16B16A16Sfloat, GraphicsPipelineConfig::DefaultColorAttachmentTransparentBlend);

		config.SetDepthStencilAttachmentFormat(vk::Format::eD24UnormS8Uint);
		config.SetDepthTest(true, false);

		config
			.AddStorageBuffer(2)				// render data
			.AddCameraUnifromDataBinding()		// set 1
			.AddBindlessMaterialTextureBinding()// set 2
			.AddLightDataBinding()				// set 3
			.AddUnifromTexture(8);				// set 0 binding 8 = shadow atlas

		config.AddDynamicState(vk::DynamicState::eCullMode);

		if (config.Validate())
			_sortedShader->Create(config);
	}

	// OIT 累积
	{
		GraphicsPipelineConfig config;
		config.vertexPath = oitVertexShaderPath;
		config.fragmentPath = oitFragmentShaderPath;

		config.AddVertexInputAttributeDescription(StaticMesh::GetVertexInputAttributeDescription());
		config.AddVertexInputBindingDescription(StaticMesh::GetVertexInputBindingDescription());

		config.AddColorAttachment(vk::Format::eR16G16B16A16Sfloat, MakeOitAccumBlend());
		config.AddColorAttachment(vk::Format::eR16Sfloat, MakeOitRevealBlend());

		config.SetDepthStencilAttachmentFormat(vk::Format::eD24UnormS8Uint);
		config.SetDepthTest(true, false);
		config.SetCullMode(vk::CullModeFlagBits::eBack);

		config
			.AddStorageBuffer(2)
			.AddCameraUnifromDataBinding()
			.AddBindlessMaterialTextureBinding()
			.AddLightDataBinding()
			.AddUnifromTexture(8);

		_oitShader = std::make_shared<GraphicsPipeline>();
		if (config.Validate())
			_oitShader->Create(config);
	}

	// OIT 合成
	{
		GraphicsPipelineConfig config;
		config.vertexPath = oitCompositeVertexShaderPath;
		config.fragmentPath = oitCompositeFragmentShaderPath;

		config.AddColorAttachment(vk::Format::eR16G16B16A16Sfloat, GraphicsPipelineConfig::DefaultColorAttachmentTransparentBlend);
		config.SetDepthStencilAttachmentFormat(vk::Format::eD24UnormS8Uint);
		config.SetDepthTest(false, false, vk::CompareOp::eAlways);
		config.SetCullMode(vk::CullModeFlagBits::eNone);
		config.SetTopology(vk::PrimitiveTopology::eTriangleList);
		config.AddUnifromTexture(1, 0);		// accum
		config.AddUnifromTexture(2, 0);		// reveal

		_oitCompositeShader = std::make_shared<GraphicsPipeline>();
		if (config.Validate())
			_oitCompositeShader->Create(config);
	}
}

bool TransparentPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	// ---- 临时调试：确认节点是否被跳过 ----
	{
		static uint64_t frameIndex = 0;
		if (++frameIndex % 30 == 1)
		{
			std::ofstream out("transparent_debug.txt", std::ios::app);
			out << "ShouldExecute frame=" << frameIndex
				<< " flag=" << state.option.flags.drawTransparent
				<< " items=" << state.objects.sceneStaticRenderData.transparentItems.size()
				<< " skinned=" << state.indirectCommands.transparentMesh_Command.size()
				<< "\n";
		}
	}

	if (!state.option.flags.drawTransparent)
		return false;

	return HasDrawableItems(state);
}

bool TransparentPass::HasDrawableItems(RenderState& state) const
{
	return !state.objects.sceneStaticRenderData.transparentItems.empty()
		|| !state.indirectCommands.transparentMesh_Command.empty();
}

void TransparentPass::PrepareStaticItems(RenderState& state, bool sorted)
{
	auto& items = state.objects.sceneStaticRenderData.transparentItems;
	auto& cullIndex = state.objects.sceneStaticRenderData.transparentItems_cullRenderIndex;

	_sortedItemIndex.clear();
	_commands.clear();
	_renderData.clear();

	if (items.empty() || cullIndex.empty())
		return;

	if (sorted)
	{
		const glm::mat4& view = state.camera.view;

		// 视空间 z 就是深度：view * center 之后取 -z，越大越远
		auto ViewDepth = [&](size_t meshIndex) -> float
			{
				AABB aabb = items[meshIndex].meshinfo.mesh->GetAABB();
				aabb.MakeTransform(items[meshIndex].transform);
				glm::vec3 center = (aabb.min + aabb.max) * 0.5f;
				glm::vec3 viewCenter = glm::vec3(view * glm::vec4(center, 1.0f));
				return -viewCenter.z;
			};

		using SortData = std::pair<float, size_t>;
		std::vector<SortData> sortedList;
		sortedList.resize(cullIndex.size(), SortData(0.f, std::numeric_limits<uint32_t>::max()));

		std::for_each(std::execution::par,
			std::views::enumerate(cullIndex).begin(),
			std::views::enumerate(cullIndex).end(),
			[&](const auto& pair)
			{
				const auto& [idx, meshIndex] = pair;
				sortedList[idx] = std::pair<float, size_t>(ViewDepth(meshIndex), meshIndex);
			}
		);

		std::sort(sortedList.begin(), sortedList.end(),
			[](const auto& lhs, const auto& rhs) { return lhs.first > rhs.first; });

		_sortedItemIndex.reserve(sortedList.size());
		for (auto& entry : sortedList)
			_sortedItemIndex.push_back(entry.second);
	}
	else
	{
		_sortedItemIndex.assign(cullIndex.begin(), cullIndex.end());
	}

	_commands.resize(_sortedItemIndex.size());
	_renderData.resize(_sortedItemIndex.size());

	auto indirectManager = IndirectDrawManager::Instance();

	indirectManager->WithSharedLock(
		{ IndirectDrawManager::SharedLockType::Mesh, IndirectDrawManager::SharedLockType::Material },
		[&]()
		{
			for (size_t i = 0; i < _sortedItemIndex.size(); i++)
			{
				auto& item = items[_sortedItemIndex[i]];
				IndirectDrawCommand& command = _commands[i];
				GeometryRenderData& data = _renderData[i];

				data.curTransform = item.transform;
				data.prevTransform = item.prevTransform;

				uint64_t materialIndex = 0;
				if (indirectManager->GetMaterialIndex_LockFree(*item.meshinfo.material, materialIndex))
					data.materialIndex = static_cast<uint32_t>(materialIndex);

				IndirectDrawMeta meta;
				if (!indirectManager->GetIndirectDrawMeta_LockFree(*item.meshinfo.mesh, meta))
				{
					// 没有 meta 的物体：instanceCount = 0，间接命令里直接跳过
					command.instanceCount = 0;
					command.firstInstance = static_cast<uint32_t>(i);
					continue;
				}

				command.indexCount = meta.indexCount;
				command.firstIndex = meta.firstIndex;
				command.vertexOffset = meta.vertexOffset;
				command.instanceCount = 1;
				command.firstInstance = static_cast<uint32_t>(i);
			}
		});
}

void TransparentPass::UploadStaticData(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd)
{
	if (!_renderDataBlock)
		_renderDataBlock = std::make_shared<StorageBlock>();

	_renderDataBlock->WriteDataAsync(cmd, _renderData.data(), _renderData.size() * sizeof(GeometryRenderData));
	_renderDataBlock->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageRead);
}

void TransparentPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{}

void TransparentPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	PrepareStaticItems(state, state.option.transparentParams.mode != Scene3DRenderOption::TransparentMode::OIT);

	if (_commands.empty() && state.indirectCommands.transparentMesh_Command.empty())
		return;

	switch (state.option.transparentParams.mode)
	{
	case Scene3DRenderOption::TransparentMode::OIT:
	{
		ExecuteOit(cmdCtx, registry, ctx, state);
		break;
	}
	case Scene3DRenderOption::TransparentMode::Sorted:
	{
		ExecuteSorted(cmdCtx, registry, ctx, state);
		break;
	}
	default:
		break;
	}
}

void TransparentPass::ExecuteSorted(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	auto targetColorBuffer = ctx.GetExternal(0);
	auto targetDepthBuffer = ctx.GetFrameLocal(0);
	auto atlasShadowMap = ctx.GetInput(0);
	if (!targetColorBuffer || !targetDepthBuffer || !atlasShadowMap)
	{
		static uint64_t nullLog = 0;
		if (++nullLog % 30 == 1)
		{
			std::ofstream out("transparent_debug.txt", std::ios::app);
			out << "ExecuteSorted SKIP-NULL color=" << (targetColorBuffer ? 1 : 0)
				<< " depth=" << (targetDepthBuffer ? 1 : 0)
				<< " atlas=" << (atlasShadowMap ? 1 : 0) << "\n";
		}
		return;
	}

	auto cmd = cmdCtx.GetCmd();
	auto indirectManager = IndirectDrawManager::Instance();

	targetColorBuffer->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	targetDepthBuffer->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);

	const auto colorView = targetColorBuffer->GetImageView(vk::ImageAspectFlagBits::eColor);
	const auto depthView = targetDepthBuffer->GetImageView(vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);

	std::shared_ptr<IndirectBufferBlock> transparentStaticCommandBuffer;
	std::shared_ptr<IndirectBufferBlock> transparentSkinnedCommandBuffer;
	if (!_commands.empty())
	{
		UploadStaticData(cmd);
		auto& commandBuffer = transparentStaticCommandBuffer;
		uint32_t size = _commands.size() * sizeof(IndirectDrawCommand);
		commandBuffer = std::make_shared<IndirectBufferBlock>(size);
		commandBuffer->WriteDataAsync(cmd, _commands.data(), size);
		commandBuffer->Barrier(cmd, BufferUsage::TransferWrite);
	}

	auto& skinnedCommands = state.indirectCommands.transparentMesh_Command;
	bool hasSkinned = !skinnedCommands.empty()
		&& state.curFrameSkinnedData
		&& state.curFrameSkinnedData->skinnedMesh_VertexBuffer
		&& state.curFrameSkinnedData->skinnedMesh_VertexBuffer->buffer
		&& state.indirectCommands.ssbo_SkinnedMesh_GeometryRenderData;

	if (hasSkinned)
	{
		auto& commandBuffer = transparentSkinnedCommandBuffer;
		uint32_t size = skinnedCommands.size() * sizeof(IndirectDrawCommand);
		commandBuffer = std::make_shared<IndirectBufferBlock>(size);
		commandBuffer->WriteDataAsync(cmd, skinnedCommands.data(), size);
		commandBuffer->Barrier(cmd, BufferUsage::TransferWrite);
	}

	DynamicViewport viewport(state.framebuffer.width, state.framebuffer.height);
	DynamicRenderInfo renderInfo;
	renderInfo
		.AddColorAttachment(colorView, vk::AttachmentLoadOp::eLoad)
		.AddDepthStencilAttachment(depthView, vk::AttachmentLoadOp::eLoad)
		.SetRenderArea(state.framebuffer.width, state.framebuffer.height);

	cmd->beginRendering(renderInfo);
	cmd->setDynamicViewport(viewport);


	if (!_commands.empty())
	{
		GraphicsBindingRecord binding;
		binding.SetStorageBlock(_renderDataBlock, 2);
		binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
		binding.SetBindlessMaterialTexture(indirectManager->GetMaterialSSBO(), BindlessTextureManager::Instance());
		binding.SetUniformTexture(atlasShadowMap, vk::ImageAspectFlagBits::eDepth, 8);
		binding.SetLightStorageData(
			state.lights.ssbo_dirLightMeta,
			state.lights.ssbo_dirLightCascade,
			state.lights.ssbo_pointLightMeta,
			state.lights.ssbo_spotLightMeta,
			state.lights.ssbo_dirLightCascadeDistances
		);

		_sortedShader->Bind(cmd, binding);

		cmd->setCullMode(vk::CullModeFlagBits::eBack);
		cmd->bindVertexBuffers(indirectManager->GetVertexBlock());
		cmd->bindIndexBuffer(indirectManager->GetIndexBlock());
		cmd->drawIndexedIndirect(transparentStaticCommandBuffer, _commands.size());
	}

	if (hasSkinned)
	{
		auto skinnedVertices = state.curFrameSkinnedData->skinnedMesh_VertexBuffer->buffer;

		GraphicsBindingRecord binding;
		binding.SetStorageBlock(state.indirectCommands.ssbo_SkinnedMesh_GeometryRenderData, 2);
		binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
		binding.SetBindlessMaterialTexture(indirectManager->GetMaterialSSBO(), BindlessTextureManager::Instance());
		binding.SetUniformTexture(atlasShadowMap, vk::ImageAspectFlagBits::eDepth, 8);
		binding.SetLightStorageData(
			state.lights.ssbo_dirLightMeta,
			state.lights.ssbo_dirLightCascade,
			state.lights.ssbo_pointLightMeta,
			state.lights.ssbo_spotLightMeta,
			state.lights.ssbo_dirLightCascadeDistances
		);

		_sortedShader->Bind(cmd, binding);

		cmd->setCullMode(vk::CullModeFlagBits::eBack);
		cmd->bindVertexBuffers(skinnedVertices);
		cmd->bindIndexBuffer(indirectManager->GetIndexBlock());
		cmd->drawIndexedIndirect(transparentSkinnedCommandBuffer, skinnedCommands.size());
	}

	cmd->endRendering();

	cmd->SubmitToQueue();
}

void TransparentPass::ExecuteOit(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	auto targetColorBuffer = ctx.GetExternal(0);
	auto targetDepthBuffer = ctx.GetFrameLocal(0);
	auto atlasShadowMap = ctx.GetInput(0);
	auto accumTexture = ctx.GetTemp(0);
	auto revealTexture = ctx.GetTemp(1);
	if (!targetColorBuffer || !targetDepthBuffer || !atlasShadowMap || !accumTexture || !revealTexture)
		return;

	auto cmd = cmdCtx.GetCmd();
	auto indirectManager = IndirectDrawManager::Instance();

	targetColorBuffer->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	targetDepthBuffer->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	accumTexture->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	revealTexture->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);

	const auto colorView = targetColorBuffer->GetImageView(vk::ImageAspectFlagBits::eColor);
	const auto depthView = targetDepthBuffer->GetImageView(vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);

	DynamicViewport viewport(state.framebuffer.width, state.framebuffer.height);

	// ---------------- 累积：所有半透明不分先后地加进 accum/reveal ----------------
	{

		std::shared_ptr<IndirectBufferBlock> transparentStaticCommandBuffer;
		std::shared_ptr<IndirectBufferBlock> transparentSkinnedCommandBuffer;
		if (!_commands.empty())
		{
			UploadStaticData(cmd);
			auto& commandBuffer = transparentStaticCommandBuffer;
			uint32_t size = _commands.size() * sizeof(IndirectDrawCommand);
			commandBuffer = std::make_shared<IndirectBufferBlock>(size);
			commandBuffer->WriteDataAsync(cmd, _commands.data(), size);
			commandBuffer->Barrier(cmd, BufferUsage::TransferWrite);
		}

		auto& skinnedCommands = state.indirectCommands.transparentMesh_Command;
		bool hasSkinned = !skinnedCommands.empty()
			&& state.curFrameSkinnedData
			&& state.curFrameSkinnedData->skinnedMesh_VertexBuffer
			&& state.curFrameSkinnedData->skinnedMesh_VertexBuffer->buffer
			&& state.indirectCommands.ssbo_SkinnedMesh_GeometryRenderData;

		if (hasSkinned)
		{
			auto& commandBuffer = transparentSkinnedCommandBuffer;
			uint32_t size = skinnedCommands.size() * sizeof(IndirectDrawCommand);
			commandBuffer = std::make_shared<IndirectBufferBlock>(size);
			commandBuffer->WriteDataAsync(cmd, skinnedCommands.data(), size);
			commandBuffer->Barrier(cmd, BufferUsage::TransferWrite);
		}

		DynamicRenderInfo oitInfo;
		oitInfo
			.AddColorAttachment(accumTexture->GetImageView(vk::ImageAspectFlagBits::eColor), vk::AttachmentLoadOp::eClear, vk::ClearColorValue{ 0.0f, 0.0f, 0.0f, 0.0f })
			.AddColorAttachment(revealTexture->GetImageView(vk::ImageAspectFlagBits::eColor), vk::AttachmentLoadOp::eClear, vk::ClearColorValue{ 1.0f, 1.0f, 1.0f, 1.0f })
			.AddDepthStencilAttachment(depthView, vk::AttachmentLoadOp::eLoad)
			.SetRenderArea(state.framebuffer.width, state.framebuffer.height);

		cmd->beginRendering(oitInfo);
		cmd->setDynamicViewport(viewport);

		GraphicsBindingRecord sharedBinding;
		sharedBinding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
		sharedBinding.SetBindlessMaterialTexture(indirectManager->GetMaterialSSBO(), BindlessTextureManager::Instance());
		sharedBinding.SetUniformTexture(atlasShadowMap, vk::ImageAspectFlagBits::eDepth, 8);
		sharedBinding.SetLightStorageData(
			state.lights.ssbo_dirLightMeta,
			state.lights.ssbo_dirLightCascade,
			state.lights.ssbo_pointLightMeta,
			state.lights.ssbo_spotLightMeta,
			state.lights.ssbo_dirLightCascadeDistances
		);

		// 静态网格
		if (!_commands.empty())
		{
			GraphicsBindingRecord binding = sharedBinding;
			binding.SetStorageBlock(_renderDataBlock, 2);
			_oitShader->Bind(cmd, binding);

			cmd->setCullMode(vk::CullModeFlagBits::eBack);
			cmd->bindVertexBuffers(indirectManager->GetVertexBlock());
			cmd->bindIndexBuffer(indirectManager->GetIndexBlock());
			cmd->drawIndexedIndirect(transparentStaticCommandBuffer, _commands.size());
		}

		// 蒙皮网格
		if (hasSkinned)
		{
			GraphicsBindingRecord binding = sharedBinding;
			binding.SetStorageBlock(state.indirectCommands.ssbo_SkinnedMesh_GeometryRenderData, 2);
			_oitShader->Bind(cmd, binding);

			cmd->bindVertexBuffers(state.curFrameSkinnedData->skinnedMesh_VertexBuffer->buffer);
			cmd->bindIndexBuffer(indirectManager->GetIndexBlock());
			cmd->drawIndexedIndirect(transparentSkinnedCommandBuffer, skinnedCommands.size());
		}

		cmd->endRendering();
	}

	accumTexture->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Read);
	revealTexture->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Read);

	// ---------------- 合成：把加权平均后的颜色按普通 alpha 混合叠到场景色 ----------------
	{
		DynamicRenderInfo info;
		info
			.AddColorAttachment(colorView, vk::AttachmentLoadOp::eLoad)
			.AddDepthStencilAttachment(depthView, vk::AttachmentLoadOp::eLoad)
			.SetRenderArea(state.framebuffer.width, state.framebuffer.height);

		cmd->beginRendering(info);
		cmd->setDynamicViewport(viewport);

		GraphicsBindingRecord binding;
		binding.SetUniformTexture(accumTexture, vk::ImageAspectFlagBits::eColor, 1);
		binding.SetUniformTexture(revealTexture, vk::ImageAspectFlagBits::eColor, 2);

		_oitCompositeShader->Bind(cmd, binding);
		cmd->draw(3, 1, 0, 0);

		cmd->endRendering();
	}

	cmd->SubmitToQueue();
}

void TransparentPass::FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	_commands.clear();
	_sortedItemIndex.clear();
	_renderData.clear();
}

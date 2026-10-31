#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/TransparentPass.h"
#include "VulkanRenderEngine/General/RenderHelper.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"
#include "VulkanRenderEngine/Base/Material.h"

#include <algorithm>
#include <execution>

using namespace VKRenderObjectData;

TransparentPass::TransparentPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath)
{
	_shader = std::make_shared<GraphicsPipeline>();

	GraphicsPipelineConfig config;
	config.vertexPath = vertexShaderPath;
	config.fragmentPath = fragmentShaderPath;

	config.AddVertexInputAttributeDescription(StaticMesh::GetVertexInputAttributeDescription());
	config.AddVertexInputBindingDescription(StaticMesh::GetVertexInputBindingDescription());

	config.AddColorAttachment(vk::Format::eR16G16B16A16Sfloat, GraphicsPipelineConfig::DefaultColorAttachmentTransparentBlend);

	config.SetDepthStencilAttachmentFormat(vk::Format::eD24UnormS8Uint);
	config.SetDepthTest(true, false);		// 测深度，但不写：半透明不能挡后面的半透明

	config
		.AddStorageBuffer(2)				// render data
		.AddCameraUnifromDataBinding()		// set 1
		.AddBindlessMaterialTextureBinding()// set 2
		.AddLightDataBinding()				// set 3
		.AddUnifromTexture(8);				// set 0 binding 8 = shadow atlas

	config.AddDynamicState(vk::DynamicState::eCullMode);

	if (config.Validate())
		_shader->Create(config);
}

bool TransparentPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!state.option.flags.drawTransparent)
		return false;

	return !state.objects.sceneStaticRenderData.transparentItems.empty()
		|| !state.indirectCommands.transparentMesh_Command.empty();
}

void TransparentPass::SortStaticItems(RenderState& state)
{
	auto& items = state.objects.sceneStaticRenderData.transparentItems;
	auto& cullIndex = state.objects.sceneStaticRenderData.transparentItems_cullRenderIndex;
	if (items.empty() || cullIndex.empty())
		return;

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
	std::vector<SortData> sorted;
	sorted.resize(cullIndex.size(), SortData(0.f, std::numeric_limits<uint32_t>::max()));

	std::for_each(std::execution::par,
		std::views::enumerate(cullIndex).begin(),
		std::views::enumerate(cullIndex).end(),
		[&](const auto& pair)
		{
			const auto& [idx, meshIndex] = pair;
			sorted[idx] = std::pair<float, size_t>(ViewDepth(meshIndex), meshIndex);
		}
	);

	std::sort(sorted.begin(), sorted.end(),
		[](const auto& lhs, const auto& rhs) { return lhs.first > rhs.first; });

	_sortedItemIndex.reserve(sorted.size());
	for (auto& entry : sorted)
		_sortedItemIndex.push_back(entry.second);

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
				command.firstInstance = static_cast<uint32_t>(i);	// 指向 _renderData[i]，与排序解耦
			}
		});

}

void TransparentPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
}

void TransparentPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	SortStaticItems(state);

	if (_commands.empty() && state.indirectCommands.transparentMesh_Command.empty())
		return;

	auto targetColorBuffer = ctx.GetExternal(0);
	auto targetDepthBuffer = ctx.GetFrameLocal(0);
	auto atlasShadowMap = ctx.GetInput(0);
	if (!targetColorBuffer || !targetDepthBuffer || !atlasShadowMap)
		return;

	auto cmd = cmdCtx.GetCmd();
	auto indirectManager = IndirectDrawManager::Instance();

	targetColorBuffer->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	targetDepthBuffer->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);

	const auto colorView = targetColorBuffer->GetImageView(vk::ImageAspectFlagBits::eColor);
	const auto depthView = targetDepthBuffer->GetImageView(vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);

	DynamicViewport viewport(state.framebuffer.width, state.framebuffer.height);

	DynamicRenderInfo renderInfo;
	renderInfo
		.AddColorAttachment(colorView, vk::AttachmentLoadOp::eLoad)
		.AddDepthStencilAttachment(depthView, vk::AttachmentLoadOp::eLoad)
		.SetRenderArea(state.framebuffer.width, state.framebuffer.height);

	cmd->beginRendering(renderInfo);
	cmd->setDynamicViewport(viewport);

	// ---------------- 静态网格：命令顺序就是由远到近的绘画顺序 ----------------
	if (!_commands.empty())
	{
		if (!_renderDataBlock)
			_renderDataBlock = std::make_shared<StorageBlock>();
		if (uint64_t size = _renderData.size() * sizeof(GeometryRenderData); _renderDataBlock->GetSize() < size)
			_renderDataBlock->SetSizeAsync(cmd, size * 1.2);

		_renderDataBlock->WriteDataAsync(cmd, _renderData.data(), _renderData.size() * sizeof(GeometryRenderData));
		_renderDataBlock->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageRead);

		auto commandBuffer = registry.GetIndirectBlock("transparentStaticCommandBuffer");
		commandBuffer->WriteDataAsync(cmd, _commands.data(), _commands.size() * sizeof(IndirectDrawCommand));
		commandBuffer->Barrier(cmd, BufferUsage::TransferWrite);

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

		_shader->Bind(cmd, binding);

		cmd->setCullMode(vk::CullModeFlagBits::eNone);
		cmd->bindVertexBuffers(indirectManager->GetVertexBlock());
		cmd->bindIndexBuffer(indirectManager->GetIndexBlock());
		cmd->drawIndexedIndirect(commandBuffer, _commands.size());
	}

	// ---------------- 蒙皮网格：SkinnedPass 已经生成好命令和顶点 ----------------
	auto& skinnedCommands = state.indirectCommands.transparentMesh_Command;
	if (!skinnedCommands.empty()
		&& state.curFrameSkinnedData
		&& state.curFrameSkinnedData->skinnedMesh_VertexBuffer
		&& state.curFrameSkinnedData->skinnedMesh_VertexBuffer->buffer
		&& state.indirectCommands.ssbo_SkinnedMesh_GeometryRenderData)
	{
		auto commandBuffer = registry.GetIndirectBlock("transparentSkinnedCommandBuffer");
		commandBuffer->WriteDataAsync(cmd, skinnedCommands.data(), skinnedCommands.size() * sizeof(IndirectDrawCommand));
		commandBuffer->Barrier(cmd, BufferUsage::TransferWrite);

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

		_shader->Bind(cmd, binding);

		cmd->setCullMode(vk::CullModeFlagBits::eNone);
		cmd->bindVertexBuffers(skinnedVertices);
		cmd->bindIndexBuffer(indirectManager->GetIndexBlock());
		cmd->drawIndexedIndirect(commandBuffer, skinnedCommands.size());
	}

	cmd->endRendering();

	cmd->SubmitToQueue();
}

void TransparentPass::FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	_commands.clear();
	_sortedItemIndex.clear();
	_renderData.clear();
}

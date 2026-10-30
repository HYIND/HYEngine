#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/SkinnedPass.h"
#include "VulkanRenderEngine/GlobalConfig.h"
#include "SpinLock.h"
#include <iostream>

#include "VulkanRenderEngine/Base/Mesh.h"
#include "VulkanRenderEngine/Base/SkinnedMesh.h"

static_assert(sizeof(Vertex) == 80);
static_assert(offsetof(Vertex, Position) == 0);
static_assert(offsetof(Vertex, Normal) == 16);
static_assert(offsetof(Vertex, TexCoords) == 32);
static_assert(offsetof(Vertex, Tangent) == 48);
static_assert(offsetof(Vertex, Bitangent) == 64);
static_assert(sizeof(SkinnedData) == 64);

using namespace VKRenderObjectData;

SkinnedPass::SkinnedPass(const std::string& skinnedComputeShaderPass)
{
	static_assert(sizeof(SkinningDispatch) == 24);
	ComputePipelineConfig config;
	config.computePath = skinnedComputeShaderPass;
	config.AddDefineMacro("MAX_BONE_INFLUENCE", GlobalConfig::Mesh_Max_Bone_Influence);
	config.AddDefineMacro("SKINNING_WORKGROUP_SIZE", SkinningWorkgroupSize);
	config
		.AddStorageBuffer(0)
		.AddStorageBuffer(1)
		.AddStorageBuffer(2)
		.AddStorageBuffer(3)
		.AddPushConstant(sizeof(SkinningDispatch));

	if (config.Validate())
		_skinningShader.Create(config);
}

SkinnedPass::~SkinnedPass()
{}

void SkinnedPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	auto cmd = cmdCtx.GetCmd();
	Execute(cmd, state);
	cmd->SubmitToQueue();
}

void SkinnedPass::Execute(const std::shared_ptr<VKWrapper::VKCommandBuffer> cmd, RenderState& state)
{
	if (!cmd) return;

	auto& skinnedRenderData = state.objects.sceneSkinnedRenderData;
	if (!_skinningShader.IsValid() || (skinnedRenderData.opaqueOneSideItems.empty() && skinnedRenderData.opaqueTwoSideItems.empty() && skinnedRenderData.transparentItems.empty()))
	{
		state.indirectCommands.skinnedMesh_OneSideCommand.clear();
		state.indirectCommands.skinnedMesh_TwoSideCommand.clear();
		state.indirectCommands.transparentMesh_Command.clear();
		state.indirectCommands.skinnedMesh_VertexBuffer.reset();
		state.indirectCommands.skinnedMesh_PaletteBuffer.reset();
		state.indirectCommands.ssbo_SkinnedMesh_TransformAndMaterialIndices.reset();
		state.indirectCommands.ssbo_SkinnedMesh_GeometryRenderData.reset();
		return;
	}

	std::vector<IndirectDrawCommand> skinnedMesh_OneSideCommand;
	std::vector<IndirectDrawCommand> skinnedMesh_TwoSideCommand;
	std::vector<IndirectDrawCommand> transparentMesh_Command;

	using DataType = std::tuple<std::vector<SceneSkinnedRenderData::SkinnedMeshItem>&, std::vector<IndirectDrawCommand>&>;
	std::vector<DataType> data = {
		DataType{skinnedRenderData.opaqueOneSideItems, skinnedMesh_OneSideCommand } ,
		DataType{skinnedRenderData.opaqueTwoSideItems,skinnedMesh_TwoSideCommand} ,
		DataType{skinnedRenderData.transparentItems,transparentMesh_Command }
	};

	std::vector<glm::mat4> paletteMatrices;
	std::vector<SkinningDispatch> dispatches;
	std::vector<TransAndMaterialIndex> skinnedMesh_TransformAndMaterialIndices;
	uint32_t outputVertexOffset = 0;
	uint32_t commandStartOffset = 0;

	for (auto& [items, commands] : data)
	{
		commands = AnlysisSkinnedIndirectCommands(items, paletteMatrices, dispatches, skinnedMesh_TransformAndMaterialIndices, commandStartOffset, outputVertexOffset);
		commandStartOffset += commands.size();
	}

	auto indirectManager = IndirectDrawManager::Instance();
	auto vertexBuffer = std::make_shared<VertexBufferBlock>();
	vertexBuffer->SetSizeAsync(cmd, static_cast<uint64_t>(outputVertexOffset) * sizeof(Vertex), false);

	auto& skinnedMesh_PaletteBuffer = state.indirectCommands.skinnedMesh_PaletteBuffer;
	skinnedMesh_PaletteBuffer = std::make_shared<StorageBlock>();
	if (!paletteMatrices.empty())
	{
		skinnedMesh_PaletteBuffer->WriteDataAsync(cmd, paletteMatrices.data(), paletteMatrices.size() * sizeof(glm::mat4));
		skinnedMesh_PaletteBuffer->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageRead);
	}

	auto& ssbo_SkinnedMesh_TransformAndMaterialIndices = state.indirectCommands.ssbo_SkinnedMesh_TransformAndMaterialIndices;
	if (!ssbo_SkinnedMesh_TransformAndMaterialIndices)
		ssbo_SkinnedMesh_TransformAndMaterialIndices = std::make_shared<StorageBlock>();
	if (!skinnedMesh_TransformAndMaterialIndices.empty())
	{
		ssbo_SkinnedMesh_TransformAndMaterialIndices->WriteDataAsync(cmd, skinnedMesh_TransformAndMaterialIndices.data(), skinnedMesh_TransformAndMaterialIndices.size() * sizeof(TransAndMaterialIndex));
		ssbo_SkinnedMesh_TransformAndMaterialIndices->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageRead);
	}

	auto& ssbo_SkinnedMesh_GeometryRenderData = state.indirectCommands.ssbo_SkinnedMesh_GeometryRenderData;
	ssbo_SkinnedMesh_GeometryRenderData = std::make_shared<StorageBlock>();
	if (!skinnedMesh_TransformAndMaterialIndices.empty())
	{
		std::vector<GeometryRenderData> geometryRenderData(skinnedMesh_TransformAndMaterialIndices.size());
		for (size_t i = 0; i < skinnedMesh_TransformAndMaterialIndices.size(); ++i)
		{
			const auto& source = skinnedMesh_TransformAndMaterialIndices[i];
			geometryRenderData[i].curTransform = source.model;
			geometryRenderData[i].prevTransform = source.model;
			geometryRenderData[i].materialIndex = source.materialIndex;
			geometryRenderData[i].motionVectorEnabled = 0;
		}
		ssbo_SkinnedMesh_GeometryRenderData->WriteDataAsync(cmd, geometryRenderData.data(), geometryRenderData.size() * sizeof(GeometryRenderData));
		ssbo_SkinnedMesh_GeometryRenderData->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageRead);
	}

	if (!dispatches.empty() && _skinningShader.IsValid())
	{
		auto sourceVertices = indirectManager->GetVertexBlock();
		auto sourceSkinnedData = indirectManager->GetSkinnedVertexBlock();
		sourceVertices->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageRead);
		sourceSkinnedData->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageRead);

		ComputeBindingRecord binding;
		binding.SetStorageBlock(sourceVertices, 0);
		binding.SetStorageBlock(sourceSkinnedData, 1);
		binding.SetStorageBlock(state.indirectCommands.skinnedMesh_PaletteBuffer, 2);
		binding.SetStorageBlock(vertexBuffer, 3);
		_skinningShader.Bind(cmd, binding);

		for (const auto& dispatch : dispatches)
		{
			_skinningShader.SetPushConstants(cmd, &dispatch, sizeof(dispatch));
			cmd->dispatch((dispatch.vertexCount + SkinningWorkgroupSize - 1) / SkinningWorkgroupSize, 1, 1);
		}
	}

	state.indirectCommands.skinnedMesh_OneSideCommand = std::move(skinnedMesh_OneSideCommand);
	state.indirectCommands.skinnedMesh_TwoSideCommand = std::move(skinnedMesh_TwoSideCommand);
	state.indirectCommands.transparentMesh_Command = std::move(transparentMesh_Command);
	state.indirectCommands.skinnedMesh_VertexBuffer = std::move(vertexBuffer);
}

std::vector<IndirectDrawCommand> SkinnedPass::AnlysisSkinnedIndirectCommands(
	const std::vector<VKRenderObjectData::SceneSkinnedRenderData::SkinnedMeshItem>& items,
	std::vector<glm::mat4>& paletteMatrices,
	std::vector<SkinningDispatch>& dispatches,
	std::vector<TransAndMaterialIndex>& skinnedMesh_TransformAndMaterialIndices,
	uint32_t commandStartOffset,
	uint32_t& outputVertexOffset
)
{
	auto indirectManager = IndirectDrawManager::Instance();
	std::vector<IndirectDrawCommand> commands(items.size());
	skinnedMesh_TransformAndMaterialIndices.resize(commandStartOffset + items.size());

	SpinLock mutex;

	using SharedLockType = IndirectDrawManager::SharedLockType;
	indirectManager->WithSharedLock(
		{ SharedLockType::Mesh, SharedLockType::Material, SharedLockType::Skinned },
		[&]()
		{
			std::for_each(std::execution::par_unseq,
				std::views::enumerate(items).begin(),
				std::views::enumerate(items).end(),
				[&](const auto& pair) {
					const auto& [itemIndex, item] = pair;

					const uint32_t transformIndex = commandStartOffset + static_cast<uint32_t>(itemIndex);

					auto& command = commands[itemIndex];
					command.indexCount = 0;
					command.instanceCount = 0;
					command.firstIndex = 0;
					command.vertexOffset = 0;
					command.firstInstance = transformIndex;

					auto& transformAndMaterial = skinnedMesh_TransformAndMaterialIndices[transformIndex];
					transformAndMaterial.model = item.transform;
					transformAndMaterial.materialIndex = 0;

					const auto& mesh = item.meshinfo.mesh;
					const auto& material = item.meshinfo.material;
					if (!mesh || !material)
						return;

					indirectManager->GetMaterialIndex_LockFree(*material, transformAndMaterial.materialIndex);

					IndirectDrawMeta meta;
					uint32_t skinnedDataIndex = 0;
					if (!indirectManager->GetIndirectDrawMeta_LockFree(*mesh, meta) ||
						!indirectManager->GetSkinnedIndex_LockFree(*mesh, skinnedDataIndex))
						return;

					const size_t paletteSize = item.palette ? item.palette->matrices.size() : 0;
					const uint32_t meshVertexCount = mesh->GetVertices().size();
					if (meshVertexCount == 0
						|| meshVertexCount != mesh->GetSkinnedData().size()
						|| meta.vertexOffset < 0
						)
						return;

					command.indexCount = meta.indexCount;
					command.firstIndex = meta.firstIndex;
					command.instanceCount = 1;

					const uint32_t paletteCount = static_cast<uint32_t>(paletteSize);
					if (paletteCount > 0)
					{
						LockGuard guard(mutex);
						command.vertexOffset = static_cast<int32_t>(outputVertexOffset);

						const uint32_t paletteOffset = static_cast<uint32_t>(paletteMatrices.size());
						paletteMatrices.append_range(item.palette->matrices);

						dispatches.push_back(SkinningDispatch{
							.vertexCount = meshVertexCount,
							.sourceVertexOffset = static_cast<uint32_t>(meta.vertexOffset),
							.skinnedDataOffset = skinnedDataIndex,
							.paletteOffset = paletteOffset,
							.paletteCount = paletteCount,
							.outputVertexOffset = outputVertexOffset
							});

						outputVertexOffset += meshVertexCount;
					}
				});
		});

	return commands;
}

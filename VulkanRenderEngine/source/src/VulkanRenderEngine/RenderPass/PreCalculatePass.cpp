#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/PreCalculatePass.h"
#include "VulkanRenderEngine/GlobalConfig.h"
//#include "VulkanRenderEngine/General/GPUTimer.h"
#include "SpinLock.h"


void PreCalculatePass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{}

PreCalculatePass::PreCalculatePass()
{}

PreCalculatePass::~PreCalculatePass()
{}

void PreCalculatePass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	//auto start = Tool::GetTimestampMircoseconds();
	std::vector<TransAndMaterialIndex> staticMesh_TransformAndMaterialIndices;
	std::vector<AABB> staticMesh_AABB;
	AnlysisIndirectCommands(state, staticMesh_TransformAndMaterialIndices, staticMesh_AABB);
	//std::cout << std::format("cost {}us\n", Tool::GetTimestampMircoseconds() - start);

	auto cmd = cmdCtx.GetCmd();
	if (!state.indirectCommands.ssbo_StaticMesh_TransformAndMaterialIndices)
		state.indirectCommands.ssbo_StaticMesh_TransformAndMaterialIndices = std::make_shared<StorageBlock>();
	state.indirectCommands.ssbo_StaticMesh_TransformAndMaterialIndices->WriteDataAsync(cmd, staticMesh_TransformAndMaterialIndices.data(), staticMesh_TransformAndMaterialIndices.size() * sizeof(TransAndMaterialIndex));
	if (!state.indirectCommands.ssbo_StaticMesh_WorldAABB)
		state.indirectCommands.ssbo_StaticMesh_WorldAABB = std::make_shared<StorageBlock>();
	state.indirectCommands.ssbo_StaticMesh_WorldAABB->WriteDataAsync(cmd, staticMesh_AABB.data(), staticMesh_AABB.size() * sizeof(AABB));
	cmd->SubmitToQueue();
}

void PreCalculatePass::AnlysisIndirectCommands(RenderState& state, std::vector<TransAndMaterialIndex>& staticMesh_TransformAndMaterialIndices, std::vector<AABB>& worldAABBs)
{
	auto indirectManager = IndirectDrawManager::Instance();

	auto& items = state.objects.sceneRenderData.opaqueMesh;
	auto& renderIndex = state.objects.sceneRenderData.opaqueMesh_renderIndex;

	auto& oneSideCommands = state.indirectCommands.staticMesh_OneSideCommand;
	auto& twoSideCommands = state.indirectCommands.staticMesh_TwoSideCommand;

	if (renderIndex.oneSideIndex.empty() && renderIndex.twoSideIndex.empty() && !items.empty())
	{
		renderIndex.oneSideIndex.resize(items.size());
		std::iota(renderIndex.oneSideIndex.begin(), renderIndex.oneSideIndex.end(), 0);
	}

	struct ProcessDatas {
		std::vector<size_t>& sideIndex;
		std::vector<IndirectDrawCommand>& commands;
	};
	std::vector<ProcessDatas> processDatas = {
		{ renderIndex.oneSideIndex,oneSideCommands},
		{ renderIndex.twoSideIndex,twoSideCommands}
	};

	size_t startInedx = 0;

	staticMesh_TransformAndMaterialIndices.resize(renderIndex.oneSideIndex.size() + renderIndex.twoSideIndex.size());
	worldAABBs.resize(renderIndex.oneSideIndex.size() + renderIndex.twoSideIndex.size());

	for (auto& data : processDatas)
	{
		auto& sideIndex = data.sideIndex;
		auto& commands = data.commands;

		if (sideIndex.empty())
			continue;

		commands.resize(sideIndex.size());

		indirectManager->WithMeshMaterialSharedLock([&]() {
			std::for_each(std::execution::par_unseq, sideIndex.begin(), sideIndex.end(),
				[&](const size_t& meshIndex)-> void
				{
					size_t index = &meshIndex - sideIndex.data();

					auto& item = items[meshIndex];
					auto& material = item.meshinfo.material;
					auto& mesh = item.meshinfo.mesh;

					IndirectDrawCommand& command = commands[index];

					auto& data = staticMesh_TransformAndMaterialIndices[startInedx + index];
					data.model = item.transform;

					auto& aabb = worldAABBs[startInedx + index];
					aabb = mesh->GetAABB();
					aabb.MakeTransform(data.model);

					indirectManager->GetMaterialIndex_LockFree(*material, data.materialIndex);

					IndirectDrawMeta meta;
					if (!indirectManager->GetIndirectDrawMeta_LockFree(*mesh, meta))
					{
						command.instanceCount = 0;
					}
					else
					{
						command.indexCount = meta.indexCount;
						command.firstIndex = meta.firstIndex;
						command.vertexOffset = meta.vertexOffset;
						command.instanceCount = 1;
						command.firstInstance = startInedx + index;
					}
				});
			});

		startInedx += sideIndex.size();
	}
}

void PreCalculatePass::FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{}

void PreCalculatePass::Execute(RenderState & state)
{
	std::vector<TransAndMaterialIndex> staticMesh_TransformAndMaterialIndices;
	std::vector<AABB> staticMesh_AABB;
	AnlysisIndirectCommands(state, staticMesh_TransformAndMaterialIndices, staticMesh_AABB);

	auto cmd = VKCONTEXT->GetCommandBuffer();
	if (!state.indirectCommands.ssbo_StaticMesh_TransformAndMaterialIndices)
		state.indirectCommands.ssbo_StaticMesh_TransformAndMaterialIndices = std::make_shared<StorageBlock>();
	state.indirectCommands.ssbo_StaticMesh_TransformAndMaterialIndices->WriteDataAsync(cmd, staticMesh_TransformAndMaterialIndices.data(), staticMesh_TransformAndMaterialIndices.size() * sizeof(TransAndMaterialIndex));
	if (!state.indirectCommands.ssbo_StaticMesh_WorldAABB)
		state.indirectCommands.ssbo_StaticMesh_WorldAABB = std::make_shared<StorageBlock>();
	state.indirectCommands.ssbo_StaticMesh_WorldAABB->WriteDataAsync(cmd, staticMesh_AABB.data(), staticMesh_AABB.size() * sizeof(AABB));
	cmd->SubmitNowAndWait();
}

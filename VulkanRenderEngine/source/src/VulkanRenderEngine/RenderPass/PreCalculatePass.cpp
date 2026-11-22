#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/PreCalculatePass.h"
#include "VulkanRenderEngine/GlobalConfig.h"
#include "SpinLock.h"
#include <limits>


PreCalculatePass::PreCalculatePass()
{}

PreCalculatePass::~PreCalculatePass()
{}

void PreCalculatePass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	auto cmd = cmdCtx.GetCmd();
	Execute(cmd, state);
	cmd->SubmitToQueue();
}

void PreCalculatePass::Execute(const std::shared_ptr<VKWrapper::VKCommandBuffer> cmd, RenderState& state)
{
	if (!cmd) return;

	std::vector<TransAndMaterialIndex> staticMesh_TransformAndMaterialIndices;
	std::vector<AABB> staticMesh_AABB;
	AnlysisIndirectCommands(state, staticMesh_TransformAndMaterialIndices, staticMesh_AABB);

	// 汇总场景包围盒。DDGI 探针体积自动拟合要用，一帧一次、纯 CPU 线性扫，开销可忽略。
	state.sceneBounds.valid = 0;
	if (!staticMesh_AABB.empty())
	{
		glm::vec3 boundsMin(std::numeric_limits<float>::max());
		glm::vec3 boundsMax(-std::numeric_limits<float>::max());
		for (const auto& aabb : staticMesh_AABB)
		{
			if (!aabb.isValid())
				continue;
			boundsMin = glm::min(boundsMin, aabb.min);
			boundsMax = glm::max(boundsMax, aabb.max);
		}
		if (boundsMin.x <= boundsMax.x)
		{
			state.sceneBounds.min = boundsMin;
			state.sceneBounds.max = boundsMax;
			state.sceneBounds.valid = 1;
		}
	}

	if (!state.indirectCommands.ssbo_StaticMesh_TransformAndMaterialIndices)
		state.indirectCommands.ssbo_StaticMesh_TransformAndMaterialIndices = std::make_shared<StorageBlock>();
	state.indirectCommands.ssbo_StaticMesh_TransformAndMaterialIndices->WriteDataAsync(cmd, staticMesh_TransformAndMaterialIndices.data(), staticMesh_TransformAndMaterialIndices.size() * sizeof(TransAndMaterialIndex));
	if (!state.indirectCommands.ssbo_StaticMesh_WorldAABB)
		state.indirectCommands.ssbo_StaticMesh_WorldAABB = std::make_shared<StorageBlock>();
	state.indirectCommands.ssbo_StaticMesh_WorldAABB->WriteDataAsync(cmd, staticMesh_AABB.data(), staticMesh_AABB.size() * sizeof(AABB));
}


void PreCalculatePass::AnlysisIndirectCommands(RenderState& state, std::vector<TransAndMaterialIndex>& staticMesh_TransformAndMaterialIndices, std::vector<AABB>& worldAABBs)
{
	auto indirectManager = IndirectDrawManager::Instance();

	auto& items = state.objects.sceneStaticRenderData.opaqueItems;
	auto& renderIndex = state.objects.sceneStaticRenderData.opaqueItems_renderIndex;

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

		using SharedLockType = IndirectDrawManager::SharedLockType;

		indirectManager->WithSharedLock(
			{ SharedLockType::Mesh, SharedLockType::Material },
			[&]()
			{
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

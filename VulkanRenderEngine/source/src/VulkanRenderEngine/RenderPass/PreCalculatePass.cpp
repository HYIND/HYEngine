#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/PreCalculatePass.h"
#include "VulkanRenderEngine/GlobalConfig.h"
//#include "VulkanRenderEngine/General/GPUTimer.h"
#include "SpinLock.h"


void PreCalculatePass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	auto indirectManager = IndirectDrawManager::Instance();

	{

		auto& items = state.objects.sceneRenderData.opaqueMesh;
		auto& renderIndex = state.objects.sceneRenderData.opaqueMesh_renderIndex;

		auto& oneSideCommands = state.indirectCommands.staticMesh_OneSideCommand;
		auto& twoSideCommands = state.indirectCommands.staticMesh_TwoSideCommand;

		if (renderIndex.oneSideIndex.empty() && renderIndex.twoSideIndex.empty() && !items.empty())
		{
			renderIndex.oneSideIndex.resize(items.size());
			std::iota(renderIndex.oneSideIndex.begin(), renderIndex.oneSideIndex.end(), 0);
		}

		std::vector<size_t> oneSideZeroMetaIndices;
		std::vector<size_t> twoSideZeroMetaIndices;

		struct ProcessDatas {
			std::vector<size_t>& sideIndex;
			std::vector<IndirectDrawCommand>& commands;
			std::vector<size_t>& zeroIndices;
		};
		std::vector<ProcessDatas> processDatas = {
			{renderIndex.oneSideIndex,oneSideCommands,oneSideZeroMetaIndices},
			{renderIndex.twoSideIndex,twoSideCommands,twoSideZeroMetaIndices}
		};

		size_t startInedx = 0;

		std::vector<TransAndMaterialIndex> staticMesh_TransformAndMaterialIndices;
		staticMesh_TransformAndMaterialIndices.resize(renderIndex.oneSideIndex.size() + renderIndex.twoSideIndex.size());

		for (auto& data : processDatas)
		{
			auto& sideIndex = data.sideIndex;
			auto& commands = data.commands;
			auto& zeroIndices = data.zeroIndices;

			SpinLock mutex_zeroIndices;

			if (sideIndex.empty())
				continue;

			commands.resize(sideIndex.size());

			std::for_each(std::execution::par, sideIndex.begin(), sideIndex.end(),
				[&](const size_t& meshIndex)-> void
				{
					size_t index = &meshIndex - sideIndex.data();

					auto& item = items[meshIndex];
					auto& material = item.meshinfo.material;
					auto& mesh = item.meshinfo.mesh;

					IndirectDrawCommand& command = commands[index];

					auto& data = staticMesh_TransformAndMaterialIndices[startInedx + index];
					data.model = item.transform;

					indirectManager->GetMaterialIndex(*material, data.materialIndex);

					IndirectDrawMeta meta;
					if (!indirectManager->GetIndirectDrawMeta(*mesh, meta))
					{
						command.instanceCount = 0;
						LockGuard guard(mutex_zeroIndices);
						zeroIndices.push_back(index);
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

			startInedx += sideIndex.size();
		}

		registry.Store("staticMesh_TransformAndMaterialIndices", std::move(staticMesh_TransformAndMaterialIndices));
	}
}

PreCalculatePass::PreCalculatePass()
{}

PreCalculatePass::~PreCalculatePass()
{}

void PreCalculatePass::EarlyExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
}

void PreCalculatePass::Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassContext& ctx, RenderState& state)
{

	if (_indirectCommandBuffer == 0)
		_indirectCommandBuffer = std::make_shared<IndirectBufferBlock>();
	if (!_ssbo_StaticMesh_TransformAndMaterialIndices)
		_ssbo_StaticMesh_TransformAndMaterialIndices = std::make_shared<StorageBlock>();

	if (state.indirectCommands.indirectCommandBuffer == 0)
		state.indirectCommands.indirectCommandBuffer = _indirectCommandBuffer;

	auto& staticMesh_TransformAndMaterialIndices = registry.Load<std::vector<TransAndMaterialIndex>>("staticMesh_TransformAndMaterialIndices");
	if (!state.indirectCommands.ssbo_StaticMesh_TransformAndMaterialIndices)
		state.indirectCommands.ssbo_StaticMesh_TransformAndMaterialIndices = _ssbo_StaticMesh_TransformAndMaterialIndices;
	state.indirectCommands.ssbo_StaticMesh_TransformAndMaterialIndices->WriteData(staticMesh_TransformAndMaterialIndices.data(), staticMesh_TransformAndMaterialIndices.size() * sizeof(TransAndMaterialIndex));
}

void PreCalculatePass::FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
}


#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/PreCalculatePass.h"
#include "VulkanRenderEngine/GlobalConfig.h"
//#include "VulkanRenderEngine/General/GPUTimer.h"
#include "SpinLock.h"


void PreCalculatePass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{

	//auto indirectManager = IndirectDrawManager::Instance();

	//{

	//	auto& items = state.objects.sceneRenderData.opaqueMesh;
	//	auto& renderIndex = state.objects.sceneRenderData.opaqueMesh_renderIndex;

	//	auto& oneSideCommands = state.indirectCommands.staticMesh_OneSideCommand;
	//	auto& twoSideCommands = state.indirectCommands.staticMesh_TwoSideCommand;

	//	size_t startInedx = 0;

	//	_staticMesh_Transforms.resize(renderIndex.oneSideIndex.size() + renderIndex.twoSideIndex.size());

	//	for (auto& indices : { renderIndex.oneSideIndex, renderIndex.twoSideIndex })
	//	{
	//		auto& commands = indices == renderIndex.oneSideIndex ?
	//			oneSideCommands
	//			: twoSideCommands;

	//		if (indices.empty())
	//			continue;

	//		commands.resize(indices.size());

	//		std::for_each(std::execution::par, indices.begin(), indices.end(),
	//			[&](const size_t& meshIndex)-> void
	//			{
	//				size_t index = &meshIndex - indices.data();

	//				auto& item = items[meshIndex];
	//				auto& material = item.meshinfo.material;
	//				auto& mesh = item.meshinfo.mesh;

	//				IndirectDrawCommand& command = commands[index];
	//				_staticMesh_Transforms[startInedx + index] = item.transform;

	//				IndirectDrawMeta meta;
	//				if (!indirectManager->GetIndirectDrawMeta(*mesh, meta))
	//					command.instanceCount = 0;
	//				else
	//				{
	//					command.indexCount = meta.indexCount;
	//					command.firstIndex = meta.firstIndex;
	//					command.vertexOffset = meta.vertexOffset;
	//					command.instanceCount = 1;
	//					command.baseInstanceIDFirst = startInedx + index;
	//				}
	//			});

	//		startInedx += indices.size();
	//	}
	//}
}

PreCalculatePass::PreCalculatePass()
{
}

PreCalculatePass::~PreCalculatePass()
{
}

void PreCalculatePass::EarlyExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
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

		std::vector<glm::mat4> staticMesh_Transforms;
		staticMesh_Transforms.resize(renderIndex.oneSideIndex.size() + renderIndex.twoSideIndex.size());

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
					staticMesh_Transforms[startInedx + index] = item.transform;

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

		registry.Store("staticMesh_Transforms", std::move(staticMesh_Transforms));
		registry.Store("oneSideZeroMetaIndices", std::move(oneSideZeroMetaIndices));
		registry.Store("twoSideZeroMetaIndices", std::move(twoSideZeroMetaIndices));
	}
}

void PreCalculatePass::Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassContext& ctx, RenderState& state)
{
	SetupIndirectDrawData(registry, state);

	if (_indirectCommandBuffer == 0)
		_indirectCommandBuffer = std::make_shared<IndirectBufferBlock>();
	if (!_ssbo_StaticMesh_Transforms)
		_ssbo_StaticMesh_Transforms = std::make_shared<StorageBlock>();

	if (state.indirectCommands.indirectCommandBuffer == 0)
		state.indirectCommands.indirectCommandBuffer = _indirectCommandBuffer;

	auto& staticMesh_Transforms = registry.Load<std::vector<glm::mat4>>("staticMesh_Transforms");
	if (!state.indirectCommands.ssbo_StaticMesh_Transforms)
		state.indirectCommands.ssbo_StaticMesh_Transforms = _ssbo_StaticMesh_Transforms;
	state.indirectCommands.ssbo_StaticMesh_Transforms->WriteData(staticMesh_Transforms.data(), staticMesh_Transforms.size() * sizeof(glm::mat4));
}

void PreCalculatePass::FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	//_staticMesh_Transforms.clear();
}

void PreCalculatePass::SetupIndirectDrawData(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{

	auto indirectManager = IndirectDrawManager::Instance();

	auto& oneSideZeroMetaIndices = registry.Load<std::vector<size_t>>("oneSideZeroMetaIndices");
	auto& twoSideZeroMetaIndices = registry.Load<std::vector<size_t>>("twoSideZeroMetaIndices");

	{
		auto& items = state.objects.sceneRenderData.opaqueMesh;
		auto& renderIndex = state.objects.sceneRenderData.opaqueMesh_renderIndex;

		struct ProcessDatas {
			std::vector<size_t>& sideIndex;
			std::vector<size_t>& zeroIndices;
		};
		std::vector<ProcessDatas> processDatas = {
			{renderIndex.oneSideIndex, oneSideZeroMetaIndices},
			{renderIndex.twoSideIndex, twoSideZeroMetaIndices}
		};

		for (auto& data : processDatas)
		{
			auto& indices = data.sideIndex;
			auto& zeroIndices = data.zeroIndices;

			for (int i = 0; i < indices.size(); i++)
			{
				auto& index = indices[i];
				auto& mesh = items[index].meshinfo.mesh;
				if (mesh->GetNeedUpdateIndricetDraw())
				{
					indirectManager->setupMesh(*mesh);
					mesh->SetNeedUpdateIndirectDraw(false);
					zeroIndices.push_back(i);
				}
			}
		}

		//for (size_t i = 0; i < items.size(); ++i)
		//{
		//	auto& mesh = items[i].meshinfo.mesh;
		//	if (mesh->GetNeedUpdateIndricetDraw())
		//	{
		//		indirectManager->setupMesh(*mesh);
		//		mesh->SetNeedUpdateIndirectDraw(false);
		//	}
		//}
	}

	{
		auto& items = state.objects.sceneRenderData.transparentMesh;
		for (auto& item : items)
		{
			auto& mesh = item.meshinfo.mesh;
			if (mesh->GetNeedUpdateIndricetDraw())
			{
				indirectManager->setupMesh(*mesh);
				mesh->SetNeedUpdateIndirectDraw(false);
			}
		}
	}

	{
		auto& items = state.objects.sceneRenderData.opaqueSkinnedModel;
		for (auto& item : items)
		{
			for (auto& info : item.models)
			{
				auto& mesh = info.mesh;
				if (mesh->GetNeedUpdateIndricetDraw())
				{
					indirectManager->setupMesh(*mesh);
					mesh->SetNeedUpdateIndirectDraw(false);
				}
			}
		}
	}

	{
		auto& items = state.objects.sceneRenderData.transparentSkinnedMesh;
		for (auto& item : items)
		{
			auto& mesh = item.meshinfo.mesh;
			if (mesh->GetNeedUpdateIndricetDraw())
			{
				indirectManager->setupMesh(*mesh);
				mesh->SetNeedUpdateIndirectDraw(false);
			}
		}
	}

	{
		auto& items = state.objects.sceneRenderData.opaqueMesh;
		auto& renderIndex = state.objects.sceneRenderData.opaqueMesh_renderIndex;

		auto& oneSideCommands = state.indirectCommands.staticMesh_OneSideCommand;
		auto& twoSideCommands = state.indirectCommands.staticMesh_TwoSideCommand;

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

		for (auto& data : processDatas)
		{
			auto& sideIndex = data.sideIndex;
			auto& commands = data.commands;
			auto& zeroIndices = data.zeroIndices;

			std::for_each(std::execution::par, zeroIndices.begin(), zeroIndices.end(),
				[&](const size_t& index)-> void
				{
					size_t meshIndex = sideIndex[index];

					auto& item = items[meshIndex];
					auto& material = item.meshinfo.material;
					auto& mesh = item.meshinfo.mesh;

					IndirectDrawCommand& command = commands[index];

					IndirectDrawMeta meta;
					if (indirectManager->GetIndirectDrawMeta(*mesh, meta))
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
	}
}

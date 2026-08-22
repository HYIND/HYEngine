#include "OpenGLRenderEngine/RenderPass/PreCalculatePass.h"
#include "OpenGLRenderEngine/OpenGLRenderConfig.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
#include "OpenGLRenderEngine/General/GPUTimer.h"
#include <execution>
#include <SpinLock.h>

static void BindVAO(GLuint VAO, GLuint VBO, GLuint EBO)
{
	glBindVertexArray(VAO);

	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);

	// 顶点位置
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
	// 顶点法线
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));
	// 顶点纹理坐标
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
	// 切线
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));
	// 副切线
	glEnableVertexAttribArray(4);
	glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));

	// 骨骼
	glEnableVertexAttribArray(5);
	glVertexAttribIPointer(5, 4, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, m_BoneIDs));

	// 骨骼
	glEnableVertexAttribArray(6);
	glVertexAttribIPointer(6, 4, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, m_BoneIDs[4]));

	// 骨骼权重
	glEnableVertexAttribArray(7);
	glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, m_Weights));

	// 骨骼权重
	glEnableVertexAttribArray(8);
	glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, m_Weights[4]));
}

void PreCalculatePass::FrameBegin(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state)
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
	//					command.indexfirst = meta.indexfirst;
	//					command.vertexFirst = meta.vertexFirst;
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
	_VAO = 0;
	_indirectCommandBuffer = 0;
}

PreCalculatePass::~PreCalculatePass()
{
	if (_VAO != 0)
		glDeleteVertexArrays(1, &_VAO);
	if (_indirectCommandBuffer == 0)
		glDeleteBuffers(1, &_indirectCommandBuffer);
}

void PreCalculatePass::EarlyExecute(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	auto indirectManager = IndirectDrawManager::Instance();

	{

		auto& items = state.objects.sceneRenderData.opaqueMesh;
		auto& renderIndex = state.objects.sceneRenderData.opaqueMesh_renderIndex;

		auto& oneSideCommands = state.indirectCommands.staticMesh_OneSideCommand;
		auto& twoSideCommands = state.indirectCommands.staticMesh_TwoSideCommand;

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
						command.indexfirst = meta.indexfirst;
						command.vertexFirst = meta.vertexFirst;
						command.instanceCount = 1;
						command.baseInstanceIDFirst = startInedx + index;
					}
				});

			startInedx += sideIndex.size();
		}

		registry.Store("staticMesh_Transforms", std::move(staticMesh_Transforms));
		registry.Store("oneSideZeroMetaIndices", std::move(oneSideZeroMetaIndices));
		registry.Store("twoSideZeroMetaIndices", std::move(twoSideZeroMetaIndices));
	}
}

void PreCalculatePass::Execute(OpenGLRenderGraph::FrameDataRegistry& registry, const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{
	SetupIndirectDrawData(registry, state);

	if (_VAO == 0)
		glGenVertexArrays(1, &_VAO);
	if (_indirectCommandBuffer == 0)
		glCreateBuffers(1, &_indirectCommandBuffer);
	if (!_ssbo_StaticMesh_Transforms)
		_ssbo_StaticMesh_Transforms = std::make_shared<SSBO>();

	if (state.indirectCommands.indirectVAO == 0)
	{
		GLuint VBO = IndirectDrawManager::Instance()->GetVBO();
		GLuint EBO = IndirectDrawManager::Instance()->GetEBO();
		if (VBO != 0 && EBO != 0)
			BindVAO(_VAO, VBO, EBO);
		state.indirectCommands.indirectVAO = _VAO;
	}
	if (state.indirectCommands.indirectCommandBuffer == 0)
		state.indirectCommands.indirectCommandBuffer = _indirectCommandBuffer;

	auto& staticMesh_Transforms = registry.Load<std::vector<glm::mat4>>("staticMesh_Transforms");
	if (!state.indirectCommands.ssbo_StaticMesh_Transforms)
		state.indirectCommands.ssbo_StaticMesh_Transforms = _ssbo_StaticMesh_Transforms;
	state.indirectCommands.ssbo_StaticMesh_Transforms->WriteData(staticMesh_Transforms.data(), staticMesh_Transforms.size() * sizeof(glm::mat4));
}

void PreCalculatePass::FrameEnd(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	//_staticMesh_Transforms.clear();
}

void PreCalculatePass::SetupIndirectDrawData(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state)
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
						command.indexfirst = meta.indexfirst;
						command.vertexFirst = meta.vertexFirst;
						command.instanceCount = 1;
						command.baseInstanceIDFirst = startInedx + index;
					}
				});

			startInedx += sideIndex.size();
		}
	}
}

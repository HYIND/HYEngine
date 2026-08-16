#include "OpenGLRenderEngine/RenderPass/PreCalculatePass.h"
#include "OpenGLRenderEngine/OpenGLRenderConfig.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
#include "OpenGLRenderEngine/General/GPUTimer.h"
#include <execution>

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

void PreCalculatePass::FrameBegin(RenderState& state)
{

	auto indirectManager = IndirectDrawManager::Instance();

	{

		auto& items = state.objects.sceneRenderData.opaqueMesh;
		auto& renderIndex = state.objects.sceneRenderData.opaqueMesh_renderIndex;

		auto& oneSideCommands = state.indirectCommands.staticMesh_OneSideCommand;
		auto& twoSideCommands = state.indirectCommands.staticMesh_TwoSideCommand;

		size_t startInedx = 0;

		_staticMesh_Transforms.resize(renderIndex.oneSideIndex.size() + renderIndex.twoSideIndex.size());

		for (auto& indices : { renderIndex.oneSideIndex, renderIndex.twoSideIndex })
		{
			auto& commands = indices == renderIndex.oneSideIndex ?
				oneSideCommands
				: twoSideCommands;

			if (indices.empty())
				continue;

			commands.resize(indices.size());

			std::for_each(std::execution::par, indices.begin(), indices.end(),
				[&](const size_t& meshIndex)-> void
				{
					size_t inedx = &meshIndex - indices.data();

					auto& item = items[meshIndex];
					auto& material = item.meshinfo.material;
					auto& mesh = item.meshinfo.mesh;

					IndirectDrawCommand& command = commands[inedx];
					_staticMesh_Transforms[startInedx + inedx] = item.transform;

					IndirectDrawMeta meta;
					if (!indirectManager->GetIndirectDrawMeta(*mesh, meta))
						command.instanceCount = 0;
					else
					{
						command.indexCount = meta.indexCount;
						command.indexfirst = meta.indexfirst;
						command.vertexFirst = meta.vertexFirst;
						command.instanceCount = 1;
						command.baseInstanceIDFirst = startInedx + inedx;
					}
				});

			startInedx += indices.size();
		}
	}
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

void PreCalculatePass::Execute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{
	if (_VAO == 0)
		glGenVertexArrays(1, &_VAO);
	if (_indirectCommandBuffer == 0)
		glGenBuffers(1, &_indirectCommandBuffer);
	if (!_ssbo_StaticMesh_Transforms)
		_ssbo_StaticMesh_Transforms = std::make_shared<SSBO>();

	if (state.indirectCommands.indirectVAO == 0)
		state.indirectCommands.indirectVAO = _VAO;
	if (state.indirectCommands.indirectCommandBuffer == 0)
		state.indirectCommands.indirectCommandBuffer = _indirectCommandBuffer;

	GLuint VBO = IndirectDrawManager::Instance()->GetVBO();
	GLuint EBO = IndirectDrawManager::Instance()->GetEBO();
	if (VBO != 0 && EBO != 0)
		BindVAO(state.indirectCommands.indirectVAO, VBO, EBO);

	if (!state.indirectCommands.ssbo_StaticMesh_Transforms)
		state.indirectCommands.ssbo_StaticMesh_Transforms = _ssbo_StaticMesh_Transforms;
	state.indirectCommands.ssbo_StaticMesh_Transforms->WriteData(_staticMesh_Transforms.data(), _staticMesh_Transforms.size() * sizeof(glm::mat4));
}

void PreCalculatePass::FrameEnd(RenderState& state)
{
	_staticMesh_Transforms.clear();
}

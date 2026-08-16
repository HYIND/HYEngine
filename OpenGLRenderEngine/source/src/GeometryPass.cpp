#include "OpenGLRenderEngine/RenderPass/GeometryPass.h"
#include "OpenGLRenderEngine/OpenGLRenderConfig.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
#include "OpenGLRenderEngine/General/IndirectDrawManager.h"
#include <execution>

GeometryPass::GeometryPass(
	const std::string& staticMeshVertexShaderPath,
	const std::string& staticMeshFragmentShaderPath,
	const std::string& skinnedMeshvertexShaderPath,
	const std::string& skinnedFragmentShaderPath
)
	:_fbo(0)
{
	_staticShader.CompileFromFile(staticMeshVertexShaderPath, staticMeshFragmentShaderPath);
	_skinnedShader.CompileFromFile(skinnedMeshvertexShaderPath, skinnedFragmentShaderPath);
}

GeometryPass::~GeometryPass()
{
	if (_fbo != 0)
		glDeleteFramebuffers(1, &_fbo);
}

void GeometryPass::BindTexToFbo(std::shared_ptr<Texture2D>& gPosition, std::shared_ptr<Texture2D>& gNormal, std::shared_ptr<Texture2D>& gAlbedoOpacity, std::shared_ptr<Texture2D>& gMetallicRoughnessMap, std::shared_ptr<Texture2D>& gMotionVectorMap, std::shared_ptr<Texture2D>& tempDepthStencilMap)
{
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gPosition->GetID(), 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gNormal->GetID(), 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gAlbedoOpacity->GetID(), 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, gMetallicRoughnessMap->GetID(), 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT4, GL_TEXTURE_2D, gMotionVectorMap->GetID(), 0);

	GLenum attachments[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3,GL_COLOR_ATTACHMENT4 };
	glDrawBuffers(5, attachments);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, tempDepthStencilMap->GetID(), 0);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "initGeometryPassData Framebuffer not complete!" << std::endl;

}

void GeometryPass::Execute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{
	if (_fbo == 0)
		glGenFramebuffers(1, &_fbo);

	auto gPosition = ctx.GetOutput(0);
	auto gNormal = ctx.GetOutput(1);
	auto gAlbedoOpacity = ctx.GetOutput(2);
	auto gMetallicRoughnessMap = ctx.GetOutput(3);
	auto gMotionVectorMap = ctx.GetOutput(4);
	auto tempDepthStencilMap = ctx.GetOutput(5);

	SetupIndirecDrawMaterial(state);

	glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
	BindTexToFbo(gPosition, gNormal, gAlbedoOpacity, gMetallicRoughnessMap, gMotionVectorMap, tempDepthStencilMap);

	glViewport(0, 0, state.framebuffer.width, state.framebuffer.height);

	glClearColor(0.f, 0.0f, 0.0f, 0.0f);
	glClearStencil(0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	RenderSceneGeometryPassStatic(state);
	RenderSceneGeometryPassSkinned(state);

}

void GeometryPass::FrameBegin(RenderState& state)
{

	{
		auto& models = state.objects.sceneRenderData.opaqueSkinnedModel;
		auto& sorts = state.objects.sceneRenderData.opaqueSkinnedModel_SortIndex;
		sorts.resize(models.size());
		for (int i = 0; i < models.size(); i++)
		{
			auto& item = models[i];
			auto& sort = sorts[i];

			sort.resize(item.models.size());
			std::iota(sort.begin(), sort.end(), 0);

			if (item.models.size() > 1)
			{
				std::sort(std::execution::par_unseq, sort.begin(), sort.end(),
					[&](int index1, int index2)-> bool
					{
						if (item.models[index1].material != item.models[index2].material)
							return item.models[index1].material < item.models[index2].material;
					}
				);
			}
		}
	}
}

void GeometryPass::FrameEnd(RenderState& state)
{
}

void GeometryPass::SetupIndirecDrawMaterial(RenderState& state)
{
	auto indirectManager = IndirectDrawManager::Instance();

	{
		auto items = state.objects.sceneRenderData.opaqueMesh;
		for (auto& item : items)
		{
			auto& material = item.meshinfo.material;
			if (material->GetNeedUpdateIndirectDraw())
			{
				indirectManager->setupMaterial(*material);
				material->SetNeedUpdateIndirectDraw(false);
			}
		}
	}

	{
		auto items = state.objects.sceneRenderData.opaqueSkinnedModel;
		for (auto& item : items)
		{
			for (auto& info : item.models)
			{
				auto& material = info.material;
				if (material->GetNeedUpdateIndirectDraw())
				{
					indirectManager->setupMaterial(*material);
					material->SetNeedUpdateIndirectDraw(false);
				}
			}
		}
	}

}

bool GeometryPass::SetupStaticBufferData(
	Shader& shader,
	std::vector<OpenGLRenderObjectData::SceneRenderData::OpaqueMeshItem>& items,
	OpenGLRenderObjectData::RenderIndex& renderIndex,
	std::vector<IndirectDrawCommand>& oneSideCommands,
	std::vector<IndirectDrawCommand>& twoSideCommands
)
{

	auto indirectManager = IndirectDrawManager::Instance();

	auto ssbo = indirectManager->GetSSBO();
	if (!ssbo || ssbo->GetID() == 0 || !shader.bindSSBO("MaterialDatas", ssbo))
		return false;

	auto renderdata_ssbo = shader.TryGetSSBO("RenderDatas");
	if (!renderdata_ssbo)
		return false;

	struct alignas(16) RenderData {
		alignas(16) glm::mat4 curTransform = glm::mat4(1.0f);
		alignas(16) glm::mat4 prevTransform;
		int materialIndex = 0;
	};

	std::vector<RenderData> renderData;
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

		std::for_each(std::execution::par, indices.begin(), indices.end(),
			[&](const size_t& meshIndex)-> void
			{
				size_t inedx = &meshIndex - indices.data();

				auto& item = items[meshIndex];
				auto& material = item.meshinfo.material;
				auto& mesh = item.meshinfo.mesh;

				IndirectDrawCommand& command = commands[inedx];
				RenderData& data = renderData[startInedx + inedx];

				data.curTransform = item.transform;
				data.prevTransform = item.prevTransform;

				uint64_t materialIndex;
				if (indirectManager->GetMaterialIndex(*material, materialIndex))
					data.materialIndex = materialIndex;

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

	renderdata_ssbo->WriteData(renderData.data(), renderData.size() * sizeof(RenderData));

	return true;
}

void GeometryPass::RenderSceneGeometryPassStatic(RenderState& state)
{
	auto& opaqueMeshes = state.objects.sceneRenderData.opaqueMesh;
	auto& renderIndex = state.objects.sceneRenderData.opaqueMesh_cullRenderIndex;
	auto& shader = _staticShader;

	if (opaqueMeshes.empty() && renderIndex.oneSideIndex.empty() && renderIndex.twoSideIndex.empty())
		return;

	std::vector<IndirectDrawCommand> oneSideCommands;
	std::vector<IndirectDrawCommand> twoSideCommands;
	if (!SetupStaticBufferData(shader, opaqueMeshes, renderIndex, oneSideCommands, twoSideCommands))
		return;

	GLboolean isCullFaceEnabled = glIsEnabled(GL_CULL_FACE);

	shader.Use();

	if (state.indirectCommands.indirectVAO == 0 || state.indirectCommands.indirectCommandBuffer == 0)
		return;

	glBindVertexArray(state.indirectCommands.indirectVAO);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, state.indirectCommands.indirectCommandBuffer);

	if (!oneSideCommands.empty())
	{
		glEnable(GL_CULL_FACE);
		glBufferData(GL_DRAW_INDIRECT_BUFFER, oneSideCommands.size() * sizeof(IndirectDrawCommand), oneSideCommands.data(), GL_DYNAMIC_DRAW);
		glMultiDrawElementsIndirect(
			GL_TRIANGLES,            // 图元类型
			GL_UNSIGNED_INT,         // 索引类型
			(void*)0,                // 起始偏移
			oneSideCommands.size(),  // 命令数量
			0);                      // 步长（0=连续存储）
	}

	if (!twoSideCommands.empty())
	{

		glDisable(GL_CULL_FACE);
		glBufferData(GL_DRAW_INDIRECT_BUFFER, twoSideCommands.size() * sizeof(IndirectDrawCommand), twoSideCommands.data(), GL_DYNAMIC_DRAW);
		glMultiDrawElementsIndirect(
			GL_TRIANGLES,            // 图元类型
			GL_UNSIGNED_INT,         // 索引类型
			(void*)0,                // 起始偏移
			twoSideCommands.size(),  // 命令数量
			0);                      // 步长（0=连续存储）
	}

	if (isCullFaceEnabled == GL_TRUE)
		glEnable(GL_CULL_FACE);
	else
		glDisable(GL_CULL_FACE);
}

void GeometryPass::RenderSceneGeometryPassSkinned(RenderState& state)
{
	auto& opaqueSinnedModels = state.objects.sceneRenderData.opaqueSkinnedModel;
	auto& renderIndexArrays = state.objects.sceneRenderData.opaqueSkinnedModel_SortIndex;

	if (opaqueSinnedModels.empty())
		return;

	GLboolean isCullFaceEnabled = glIsEnabled(GL_CULL_FACE);

	auto& shader = _skinnedShader;

	shader.Use();

	std::shared_ptr<Material> cur_Material;
	glm::mat4 cur_Model = glm::mat4(1.0f);
	glm::mat4 cur_PreModel = glm::mat4(1.0f);

	RenderHelp::SetupAnimatorGroupData(shader, {});
	shader.setMat4("model", cur_Model);
	shader.setMat4("prevModel", cur_PreModel);

	for (size_t i = 0; i < opaqueSinnedModels.size(); i++)
	{
		auto& item = opaqueSinnedModels[i];

		if (cur_Model != item.transform)
		{
			shader.setMat4("model", item.transform);
			cur_Model = item.transform;
		}
		if (cur_PreModel != item.prevTransform)
		{
			shader.setMat4("prevModel", item.prevTransform);
			cur_PreModel = item.prevTransform;
		}
		RenderHelp::SetupAnimatorGroupData(shader, *item.animators);

		auto& sort = renderIndexArrays[i];
		for (int i = 0; i < sort.size(); i++)
		{
			auto meshIndex = sort[i];
			auto& meshinfo = item.models[meshIndex];
			if (cur_Material != meshinfo.material)
			{
				meshinfo.ApplyMaterialWithSideOption();
				cur_Material = meshinfo.material;
			}
			//meshinfo.mesh->SetDirty();
			meshinfo.DrawGeometry(shader);
		}
	}

	if (isCullFaceEnabled == GL_TRUE)
		glEnable(GL_CULL_FACE);
	else
		glDisable(GL_CULL_FACE);
}
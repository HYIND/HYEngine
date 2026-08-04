#include "OpenGLRenderEngine/RenderPass/GeometryPass.h"
#include "OpenGLRenderEngine/OpenGLRenderConfig.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
#include <execution>

GeometryPass::GeometryPass(
	const std::string& staticMeshVertexShaderPath,
	const std::string& skinnedMeshvertexShaderPath,
	const std::string& fragmentShaderPath
)
	:_fbo(0)
{
	_staticShader.CompileFromFile(staticMeshVertexShaderPath, fragmentShaderPath);
	_skinnedShader.CompileFromFile(skinnedMeshvertexShaderPath, fragmentShaderPath);
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

void GeometryPass::Excute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{
	if (_fbo == 0)
		glGenFramebuffers(1, &_fbo);

	auto gPosition = ctx.GetOutput(0);
	auto gNormal = ctx.GetOutput(1);
	auto gAlbedoOpacity = ctx.GetOutput(2);
	auto gMetallicRoughnessMap = ctx.GetOutput(3);
	auto gMotionVectorMap = ctx.GetOutput(4);
	auto tempDepthStencilMap = ctx.GetOutput(5);


	glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
	BindTexToFbo(gPosition, gNormal, gAlbedoOpacity, gMetallicRoughnessMap, gMotionVectorMap, tempDepthStencilMap);

	glViewport(0, 0, state.framebuffer.width, state.framebuffer.height);

	glClearColor(0.f, 0.0f, 0.0f, 0.0f);
	glClearStencil(0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	_staticShader.Use();

	RenderHelp::renderSceneGeometryPassStatic(
		state, _staticShader,
		state.objects.sceneRenderData.opaqueMesh,
		state.objects.sceneRenderData.opaqueMesh_SortIndex
	);

	//RenderHelp::renderSceneGeometryPassStatic(
	//	state, _staticShader,
	//	state.objects.sceneRenderData.opaqueMesh,
	//	_renderIndex
	//);

	_skinnedShader.Use();
	RenderHelp::renderSceneGeometryPassSkinned(
		state, _skinnedShader,
		state.objects.sceneRenderData.opaqueSkinnedModel,
		state.objects.sceneRenderData.opaqueSkinnedModel_SortIndex
	);
}

void GeometryPass::FrameBegin(RenderState& state)
{
	//{
	//	Frustum& frustum = state.camera.frustum;
	//	auto& opaqueMeshes = state.objects.sceneRenderData.opaqueMesh;

	//	if (!opaqueMeshes.empty())
	//	{
	//		_frustumCullResult.resize(opaqueMeshes.size(), false);

	//		std::for_each(std::execution::par, opaqueMeshes.begin(), opaqueMeshes.end(),
	//			[&](OpenGLRenderObjectData::SceneRenderData::OpaqueMeshItem& mesh)
	//			{

	//				size_t index = &mesh - opaqueMeshes.data();
	//				AABB aabbworld = mesh.meshinfo.mesh->GetAABB();
	//				aabbworld.MakeTransform(mesh.transform);

	//				bool result = frustum.IsAABBOnFrustum(aabbworld);
	//				_frustumCullResult[index] = !result;
	//			});

	//		_renderIndex.reserve(opaqueMeshes.size());
	//		for (size_t i = 0; i < opaqueMeshes.size(); i++)
	//		{
	//			if (_frustumCullResult[i]) continue;
	//			_renderIndex.push_back(i);
	//		}

	//		if (_renderIndex.size() > 1)
	//		{
	//			std::sort(_renderIndex.begin(), _renderIndex.end(),
	//				[&](size_t index1, size_t index2)-> bool
	//				{
	//					return opaqueMeshes[index1].meshinfo.material < opaqueMeshes[index2].meshinfo.material;
	//				}
	//			);
	//		}
	//	}
	//}

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
	_frustumCullResult.clear();
	//_renderIndex.clear();
}

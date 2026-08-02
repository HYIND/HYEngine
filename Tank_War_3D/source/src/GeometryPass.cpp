#include "OpenGLRenderEngine/RenderPass/GeometryPass.h"
#include "OpenGLRenderEngine/OpenGLRenderConfig.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"

GeometryPassPass::GeometryPassPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath)
	:_fbo(0)
{
	_shader.CompileFromFile(vertexShaderPath, fragmentShaderPath);
}

void GeometryPassPass::BindTexToFbo(std::shared_ptr<Texture2D>& gPosition, std::shared_ptr<Texture2D>& gNormal, std::shared_ptr<Texture2D>& gAlbedoOpacity, std::shared_ptr<Texture2D>& gMetallicRoughnessMap, std::shared_ptr<Texture2D>& gMotionVectorMap, std::shared_ptr<Texture2D>& tempDepthStencilMap)
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

void GeometryPassPass::Excute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
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

	_shader.Use();

	RenderHelp::renderGeometryPassScene(state, _shader, state.objects.sceneItems, state.objectsGroupMapper.sceneItemsGroupMapper);
}

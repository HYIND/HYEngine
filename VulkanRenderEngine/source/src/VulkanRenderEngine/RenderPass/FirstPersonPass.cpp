#include "OpenGLRenderEngine/RenderPass/FirstPersonPass.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
#include "glm/gtc/matrix_transform.hpp"

FirstPersonPass::FirstPersonPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath)
	:_shader(vertexShaderPath, fragmentShaderPath)
{
}

void FirstPersonPass::Draw(RenderState& state)
{
	//if (state.objects.firstPersonItems.empty())
	//	return;

	////glBindFramebuffer(GL_FRAMEBUFFER, state.framebuffer.firstPersonFbo);

	//glViewport(0, 0, state.framebuffer.width, state.framebuffer.height);

	//glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	//glClearStencil(0);
	//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	//_shader.Use();

	//RenderHelp::renderFirstPersonScene(state, _shader, state.objects.firstPersonItems);
}

void RenderHelp::renderFirstPerson(
	RenderState& state, std::shared_ptr<VKWrapper::VKCommandBuffer> cmd,
	std::vector<OpenGLRenderObjectData::FirstPersonRenderData::OpaqueMeshItem>& opaqueMeshes,
	std::vector<OpenGLRenderObjectData::FirstPersonRenderData::OpaqueSkinnedModelItem>& opaqueSkinned,
	std::vector<OpenGLRenderObjectData::FirstPersonRenderData::TransparentMeshItem>& transparentMeshes,
	std::vector<OpenGLRenderObjectData::FirstPersonRenderData::OpaqueSkinnedModelItem>& transparentModels
)
{
	//for (auto& item : items)
	//{
	//	SetupAnimatorGroupData(shader, item.animatorViews);

	//	shader.setMat4("CmaeraView", item.cameraView);

	//	item.meshinfo.Draw(shader);
	//}
}
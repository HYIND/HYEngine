#include "OpenGLRenderEngine/RenderPass/SkyBoxPass.h"
#include "Manager/ResourceManager.h"

void initskybox(GLuint& skyboxVAO, GLuint& skyboxVBO)
{
	static float skyboxVertices[] = {// positions          
		-1.0f,  1.0f, -1.0f,
		-1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,
		 1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,

		-1.0f, -1.0f,  1.0f,
		-1.0f, -1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f,  1.0f,
		-1.0f, -1.0f,  1.0f,

		 1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,

		-1.0f, -1.0f,  1.0f,
		-1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f, -1.0f,  1.0f,
		-1.0f, -1.0f,  1.0f,

		-1.0f,  1.0f, -1.0f,
		 1.0f,  1.0f, -1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		-1.0f,  1.0f,  1.0f,
		-1.0f,  1.0f, -1.0f,

		-1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f,  1.0f,
		 1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f,  1.0f,
		 1.0f, -1.0f,  1.0f
	};

	glGenVertexArrays(1, &skyboxVAO);
	glGenBuffers(1, &skyboxVBO);
	glBindVertexArray(skyboxVAO);
	glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
}

SkyBoxPass::SkyBoxPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath)
	:_shader(vertexShaderPath, fragmentShaderPath)
{
	initskybox(_skyboxVAO, _skyboxVBO);
}

bool SkyBoxPass::ShouldExecute(RenderState& state) const
{
	return state.flags.skyboxOn && ResFactory->GetTextureCubeRes(ResName::skybox2);
}

void SkyBoxPass::Excute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{
	auto skyboxCubeMap = ResFactory->GetTextureCubeRes(ResName::skybox2);
	if (!skyboxCubeMap)
		return;

	glBindFramebuffer(GL_FRAMEBUFFER, ctx.renderTargetFBO);

	glViewport(0, 0, state.framebuffer.width, state.framebuffer.height);

	glDepthFunc(GL_LEQUAL);

	//glDepthMask(GL_FALSE);
	_shader.Use();
	glBindVertexArray(_skyboxVAO);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxCubeMap);
	_shader.setInt("skybox", 0);

	glm::mat4 skyboxview = glm::mat4(glm::mat3(state.camera.view));
	_shader.setMat4("rotview", skyboxview);

	glDrawArrays(GL_TRIANGLES, 0, 36);
	//glDepthMask(GL_TRUE);
	glDepthFunc(GL_LESS);
}
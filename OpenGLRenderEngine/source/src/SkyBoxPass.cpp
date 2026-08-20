#include "OpenGLRenderEngine/RenderPass/SkyBoxPass.h"

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
	:_skyboxVAO(0), _skyboxVBO(0)
{
	_shader.CompileFromFile(vertexShaderPath, fragmentShaderPath);
}

SkyBoxPass::~SkyBoxPass() 
{ 
	if (_skyboxVAO != 0)
		glDeleteVertexArrays(1, &_skyboxVAO); 
	if (_skyboxVBO != 0)
		glDeleteBuffers(1, &_skyboxVBO);
}

bool SkyBoxPass::ShouldExecute(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return state.option.flags.skyboxOn && state.skybox.cube;
}

void SkyBoxPass::Execute(OpenGLRenderGraph::FrameDataRegistry& registry, const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{
	auto skyboxCubeMap = state.skybox.cube;
	if (!skyboxCubeMap)
		return;

	glBindFramebuffer(GL_FRAMEBUFFER, ctx.renderTargetFBO);
	glViewport(0, 0, state.framebuffer.width, state.framebuffer.height);

	glDepthFunc(GL_LEQUAL);


	if (_skyboxVAO == 0 || _skyboxVBO == 0)
		initskybox(_skyboxVAO, _skyboxVBO);

	//glDepthMask(GL_FALSE);
	_shader.Use();
	glBindVertexArray(_skyboxVAO);
	_shader.setTexture(skyboxCubeMap, "skybox", 0);

	glm::mat4 skyboxview = glm::mat4(glm::mat3(state.camera.view));
	_shader.setMat4("rotview", skyboxview);

	glDrawArrays(GL_TRIANGLES, 0, 36);
	//glDepthMask(GL_TRUE);
	glDepthFunc(GL_LESS);
}
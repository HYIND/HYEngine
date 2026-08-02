#include "OpenGLRenderEngine/RenderPass/SSRPass.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
#include "Manager/ResourceManager.h"
#include "OpenGLRenderEngine/General/GPUTimer.h"
#include "glm/gtc/matrix_transform.hpp"

#define work_size_x 16
#define work_size_y 16

SSRPass::SSRPass(
	const std::string& ssrComputerShaderPath,
	const std::string& blurComputerShaderPath
)
{
	_ssrShader.AddDefineMacro("MAX_STEPS", std::to_string(OpenGLRenderConfig::SSR_Max_Step));
	_ssrShader.AddDefineMacro("Max_Bounce_limit", std::to_string(OpenGLRenderConfig::SSR_Max_Bounce_limit));
	_ssrShader.CompileFromFile(ssrComputerShaderPath);
	_blurShader.CompileFromFile(blurComputerShaderPath);
}

bool SSRPass::ShouldExecute(RenderState& state) const
{
	if (!state.flags.ssrOn || state.ssrTraceParams.maxBounceLimit <= 0 || state.objects.sceneItems.empty())
		return false;
	return true;
}

void SSRPass::Excute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{
	if (!ShouldExecute(state))
		return;

	FrameRenderData data;
	data.scrSize = glm::ivec2(state.framebuffer.width, state.framebuffer.height);
	data.drawSize = data.scrSize;

	data.originTexture = ctx.GetTemp(0);
	data.outPutTexture = ctx.GetOutput(0);

	data.gPosition = ctx.GetInput(0);
	data.gNormal = ctx.GetInput(1);
	data.gAlbedoOpacity = ctx.GetInput(2);
	data.gMetallicRoughness = ctx.GetInput(3);

	data.colorMap = ctx.GetExternal(0);
	data.depthMap = ctx.GetExternal(1);

	if (!data.originTexture || !data.outPutTexture)
		return;

	if (!DrawSSR(data, state)) return;
	if (!DrawBlur(data, state)) return;

	//DrawTexture(originTexture, "test_ori_SSRPass.png", originTexture->GetWidth(), originTexture->GetHeight());
	//DrawTexture(blurTexture, "test_blur_SSRPass.png", blurTexture->GetWidth(), blurTexture->GetHeight());
	//DrawTexture(outPutTexture, "test_outPut_SSRPass.png", outPutTexture->GetWidth(), outPutTexture->GetHeight());
}

bool SSRPass::DrawSSR(FrameRenderData& data, RenderState& state)
{
	auto& targetTex = data.originTexture;

	if (!targetTex || targetTex->IsEmpty())
		return false;


	GLfloat clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glClearTexImage(targetTex->GetID(), 0, GL_RGBA, GL_FLOAT, clearColor);

	_ssrShader.Use();

	_ssrShader.setIVec2("screenSize", data.drawSize);

	//光追参数
	_ssrShader.setFloat("tMin", state.ssrTraceParams.tMin);
	_ssrShader.setFloat("tMax", state.ssrTraceParams.tMax);
	_ssrShader.setInt("maxBounce", std::min(state.ssrTraceParams.maxBounceLimit, OpenGLRenderConfig::SSR_Max_Bounce_limit));

	_ssrShader.setTexture(data.gPosition, "gPosition", 5);
	_ssrShader.setTexture(data.gNormal, "gNormal", 6);
	_ssrShader.setTexture(data.gAlbedoOpacity, "gAlbedoOpacity", 7);
	_ssrShader.setTexture(data.gMetallicRoughness, "gMetallicRoughness", 8);
	_ssrShader.setTexture(data.colorMap, "colorMap", 9);
	_ssrShader.setTexture(data.depthMap, "depthMap", 10);

	glBindImageTexture(0, targetTex->GetID(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
	glDispatchCompute((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	return true;
}

bool SSRPass::DrawBlur(FrameRenderData& data, RenderState& state)
{
	std::shared_ptr<Texture2D>& srcTex = data.originTexture;
	std::shared_ptr<Texture2D>& targetTex = data.outPutTexture;

	if (!srcTex || srcTex->IsEmpty())
		return false;

	if (!targetTex || targetTex->IsEmpty())
		return false;

	GLfloat clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glClearTexImage(targetTex->GetID(), 0, GL_RGBA, GL_FLOAT, clearColor);

	int width = srcTex->GetWidth();
	int height = srcTex->GetHeight();

	_blurShader.Use();

	_blurShader.setIVec2("screenSize", data.drawSize);

	_blurShader.setTexture(srcTex, "rawTexture", 4);
	_blurShader.setTexture(data.gNormal, "gNormal", 6);
	_blurShader.setTexture(data.depthMap, "depthMap", 10);

	_blurShader.setFloat("blurRadius", OpenGLRenderConfig::SSR_BlurRadius);
	_blurShader.setFloat("blurDepthWeight", OpenGLRenderConfig::SSR_BlurDepthWeight);
	_blurShader.setInt("kernelSize", OpenGLRenderConfig::SSR_BlurKernelSize);
	_blurShader.setFloat("sigma", OpenGLRenderConfig::SSR_BlurGaussSigma);

	glBindImageTexture(0, targetTex->GetID(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
	glDispatchCompute((width + work_size_x - 1) / work_size_x, (height + work_size_y - 1) / work_size_y, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	return true;
}

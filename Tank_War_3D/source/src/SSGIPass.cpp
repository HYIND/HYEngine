#include "OpenGLRenderEngine/RenderPass/SSGIPass.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
#include "OpenGLRenderEngine/General/GPUTimer.h"
#include "Manager/ResourceManager.h"
#include "glm/gtc/matrix_transform.hpp"

#define work_size_x 16
#define work_size_y 16

SSGIPass::SSGIPass(
	const std::string& ssgiComputerShaderPath,
	const std::string& spatialDenoisingComputerShaderPath,
	const std::string& temporalDenoisingComputerShaderPath
)
	:
	_enable(false)
{
	_ssgiShader.AddDefineMacro("work_size_x", work_size_x);
	_ssgiShader.AddDefineMacro("work_size_y", work_size_y);
	_ssgiShader.AddDefineMacro("MAX_STEPS", OpenGLRenderConfig::SSGI_Max_Step);
	_ssgiShader.CompileFromFile(ssgiComputerShaderPath);

	_spatialDenoisingShader.AddDefineMacro("work_size_x", work_size_x);
	_spatialDenoisingShader.AddDefineMacro("work_size_y", work_size_y);
	_spatialDenoisingShader.CompileFromFile(spatialDenoisingComputerShaderPath);

	_temporalDenoisingShader.AddDefineMacro("work_size_x", work_size_x);
	_temporalDenoisingShader.AddDefineMacro("work_size_y", work_size_y);
	_temporalDenoisingShader.CompileFromFile(temporalDenoisingComputerShaderPath);
}

bool SSGIPass::ShouldExecute(RenderState& state) const
{
	SetEnable(state.flags.ssgiOn);
	if (!_enable || state.ssgiTraceParams.maxBounceLimit < 0)
		return false;
	return true;
}

void SSGIPass::Excute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{
	if (!ShouldExecute(state))
		return;


	FrameRenderData data;
	data.scrSize = glm::ivec2(state.framebuffer.width, state.framebuffer.height);
	data.drawSize = data.scrSize;
	data.originTexture = ctx.GetTemp(0);
	data.spatialDenoisingTexture = ctx.GetTemp(1);

	data.outPutTexture = ctx.GetOutput(0);

	data.historyColorTexture = ctx.GetPersitent(0);

	data.gPosition = ctx.GetInput(0);
	data.gNormal = ctx.GetInput(1);
	data.gAlbedoOpacity = ctx.GetInput(2);
	data.gMetallicRoughness = ctx.GetInput(3);
	data.gMotionVector = ctx.GetInput(4);
	data.ssaoTexture = ctx.GetInput(5);

	data.colorMap = ctx.GetExternal(0);
	data.depthMap = ctx.GetExternal(1);

	if (!DrawSSGI(data, state)) return;
	if (!DrawSpatialDenoising(data, state)) return;
	if (!DrawTemporalDenoising(data, state)) return;

	if (data.outPutTexture && data.historyColorTexture)
		Texture2D::CopyTexture(data.outPutTexture, data.historyColorTexture);

	//RENDERCONTEXMANAGER->WithTempReleaseMainOpenGLBind([&]()->void {
	//	THREADCONTEXT->UnBind();
	//	auto task1 = CoroTask::Run([&]()-> void {DrawTexture(_originTexture, "temp/SSGIPass1_oris.png"); });
	//	auto task2 = CoroTask::Run([&]()-> void {DrawTexture(_spatialDenoisingTexture, "temp/SSGIPass2_spatialDenoisingr.png"); });
	//	auto task3 = CoroTask::Run([&]()-> void {DrawTexture(_temporalDenoisingTexture, "temp/SSGIPass3_temporalDenoisingTexture.png"); });
	//	auto task4 = CoroTask::Run([&]()-> void {DrawTexture(_outPutTexture, "temp/SSGIPass4_output.png"); });
	//	task1.sync_wait();
	//	task2.sync_wait();
	//	task3.sync_wait();
	//	task4.sync_wait();
	//	THREADCONTEXT->Bind();
	//	});
}

void SSGIPass::SetEnable(bool enable) const
{
	if (_enable == enable)
		return;
	_enable = enable;
	if (_enable)
		_firstDrawTemporal = true;
}

bool SSGIPass::DrawSSGI(FrameRenderData& data, RenderState& state)
{
	auto& target = data.originTexture;

	if (!target || target->IsEmpty())
		return false;

	GLfloat clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glClearTexImage(target->GetID(), 0, GL_RGBA, GL_FLOAT, clearColor);

	_ssgiShader.Use();

	_ssgiShader.setIVec2("screenSize", data.drawSize);

	_ssgiShader.setFloat("tMin", state.ssgiTraceParams.tMin);
	_ssgiShader.setFloat("tMax", state.ssgiTraceParams.tMax);
	_ssgiShader.setInt("sampleRayCount", OpenGLRenderConfig::SSGI_NUM_SAMPLES);
	_ssgiShader.setFloat("sampleIndirectClampValue", OpenGLRenderConfig::SSGI_Sample_Indirect_Clamp_Value);
	_ssgiShader.setFloat("GIIntensity", OpenGLRenderConfig::SSGI_GIIntensity);
	_ssgiShader.setFloat("AOIntensity", OpenGLRenderConfig::SSGI_AOIntensity);


	_ssgiShader.setTexture(data.gPosition, "gPosition", 5);
	_ssgiShader.setTexture(data.gNormal, "gNormal", 6);
	_ssgiShader.setTexture(data.gAlbedoOpacity, "gAlbedoOpacity", 7);
	_ssgiShader.setTexture(data.gMetallicRoughness, "gMetallicRoughness", 8);
	_ssgiShader.setTexture(data.colorMap, "colorMap", 9);
	_ssgiShader.setTexture(data.depthMap, "depthMap", 10);

	if (data.ssaoTexture) _ssgiShader.setTexture(data.ssaoTexture, "SSAOMap", 11);

	_ssgiShader.setInt("frameIndex", state.renderRecord.frameIndex % 100000);

	glBindImageTexture(0, target->GetID(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
	glDispatchCompute((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	return true;
}

bool SSGIPass::DrawSpatialDenoising(FrameRenderData& data, RenderState& state)
{
	auto& srcTex = data.originTexture;
	auto& targetTex = data.spatialDenoisingTexture;

	if (!srcTex || srcTex->IsEmpty())
		return false;

	if (!targetTex || targetTex->IsEmpty())
		return false;

	GLfloat clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glClearTexImage(targetTex->GetID(), 0, GL_RGBA, GL_FLOAT, clearColor);

	_spatialDenoisingShader.Use();

	_spatialDenoisingShader.setIVec2("screenSize", data.drawSize);

	_spatialDenoisingShader.setTexture(srcTex, "rawTexture", 4);
	_spatialDenoisingShader.setTexture(data.gNormal, "gNormal", 6);
	_spatialDenoisingShader.setTexture(data.depthMap, "depthMap", 10);

	_spatialDenoisingShader.setFloat("blurRadius", OpenGLRenderConfig::SSGI_BlurRadius);
	_spatialDenoisingShader.setFloat("blurDepthWeight", OpenGLRenderConfig::SSGI_BlurDepthWeight);
	_spatialDenoisingShader.setInt("kernelSize", OpenGLRenderConfig::SSGI_BlurKernelSize);
	_spatialDenoisingShader.setFloat("sigma", OpenGLRenderConfig::SSGI_BlurGaussSigma);

	glBindImageTexture(0, targetTex->GetID(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
	glDispatchCompute((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	return true;
}

bool SSGIPass::DrawTemporalDenoising(FrameRenderData& data, RenderState& state)
{
	auto& srcTex = data.spatialDenoisingTexture;
	auto& targetTex = data.outPutTexture;

	if (!srcTex || srcTex->IsEmpty())
		return false;

	if (!targetTex || targetTex->IsEmpty())
		return false;

	if (_firstDrawTemporal || !data.gMotionVector || !data.historyColorTexture)
	{
		Texture2D::CopyTexture(srcTex, targetTex);
		_firstDrawTemporal = false;
		return true;
	}

	GLfloat clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glClearTexImage(targetTex->GetID(), 0, GL_RGBA, GL_FLOAT, clearColor);

	int width = srcTex->GetWidth();
	int height = srcTex->GetHeight();

	_temporalDenoisingShader.Use();
	_temporalDenoisingShader.setIVec2("screenSize", glm::ivec2(width, height));
	_temporalDenoisingShader.setTexture(srcTex, "rawTexture", 4);
	_temporalDenoisingShader.setTexture(data.historyColorTexture, "historyColorTexture", 5);
	_temporalDenoisingShader.setTexture(data.gMotionVector, "motionMap", 6);

	glBindImageTexture(0, targetTex->GetID(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
	glDispatchCompute((width + work_size_x - 1) / work_size_x, (height + work_size_y - 1) / work_size_y, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	return true;
}

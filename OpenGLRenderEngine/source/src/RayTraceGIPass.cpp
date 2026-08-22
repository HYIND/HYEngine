#include "OpenGLRenderEngine/RenderPass/RayTraceGIPass.h"

#define work_size_x 16
#define work_size_y 16


static void WaitFence(GLsync& fence)
{
	if (fence && glIsSync(fence))
	{
		glWaitSync(fence, 0, GL_TIMEOUT_IGNORED);
		glDeleteSync(fence);
		fence = NULL;
	}
}

RayTraceGIPass::RayTraceGIPass(
	const std::string& rayTraceComputerShaderPath,
	const std::string& spatialDenoisingComputerShaderPath,
	const std::string& temporalDenoisingComputerShaderPath,
	const std::string& scaleComputerShaderPath
)
	:
	_firstDrawTemporal(true),
	_enable(false)
{
	_rayTraceShader_useGbuffer.AddDefineMacro("Max_Recursive_Depth", OpenGLRenderConfig::RayTrace_Max_Recursive_Depth);
	_rayTraceShader_useGbuffer.AddDefineMacro("Max_Bounce_limit", OpenGLRenderConfig::RayTrace_Max_Bounce_limit);

	_rayTraceShader_useGbuffer.CompileFromFile(rayTraceComputerShaderPath);

	_spatialDenoisingShader.AddDefineMacro("work_size_x", work_size_x);
	_spatialDenoisingShader.AddDefineMacro("work_size_y", work_size_y);
	_spatialDenoisingShader.CompileFromFile(spatialDenoisingComputerShaderPath);

	_temporalDenoisingShader.AddDefineMacro("work_size_x", work_size_x);
	_temporalDenoisingShader.AddDefineMacro("work_size_y", work_size_y);
	_temporalDenoisingShader.CompileFromFile(temporalDenoisingComputerShaderPath);

	_scaleShader.CompileFromFile(scaleComputerShaderPath);
}

RayTraceGIPass::~RayTraceGIPass()
{
}

bool RayTraceGIPass::ShouldExecute(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!_enable || state.option.ssgiTraceParams.maxBounceLimit < 0)
		return false;
	return true;
}

void RayTraceGIPass::Execute(OpenGLRenderGraph::FrameDataRegistry& registry, const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{
	if (!ShouldExecute(registry, state))
		return;

	FrameRenderData data;
	data.scrSize = glm::ivec2(state.framebuffer.width, state.framebuffer.height);
	data.drawSize = data.scrSize;
	data.gPosition = ctx.GetInput(0);
	data.gNormal = ctx.GetInput(1);
	data.gAlbedoOpacity = ctx.GetInput(2);
	data.gMetallicRoughness = ctx.GetInput(3);
	data.atlasShadowMap = ctx.GetInput(4);
	data.ssaoMap = ctx.GetInput(5);
	data.gMotionVector = ctx.GetInput(6);

	data.sceneDepthBuffer = ctx.GetExternal(0);

	data.originTexture = ctx.GetTemp(0);
	data.spatialDenoisingTexture = ctx.GetTemp(1);

	data.historyColorTexture = ctx.GetPersitent(0);

	data.outPutTexture = ctx.GetOutput(0);

	if (!DrawRayTraceGI(data, state)) return;
	if (!DrawSpatialDenoising(data, state)) return;
	if (!DrawTemporalDenoising(data, state)) return;

	if (data.outPutTexture && data.historyColorTexture)
		Texture2D::CopyTexture(data.outPutTexture, data.historyColorTexture);

	//if (!DrawScale(data, state)) return;

	//RENDERCONTEXMANAGER->WithTempReleaseMainOpenGLBind([&]()->void {
	//	THREADCONTEXT->UnBind();
	//	auto task1 = CoroTask::Run([&]()-> void {DrawTexture(data.outPutTexture, "temp/test_RayTraceGIPass.png"); });
	//	task1.sync_wait();
	//	THREADCONTEXT->Bind();
	//	});
}

void RayTraceGIPass::FrameBegin(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	SetEnable(state.option.flags.rayTraceGIOn);
}

void RayTraceGIPass::SetGeneralBuffer(std::shared_ptr<RayTraceGeneralBuffer> buffer) {
	_buffers = buffer;
}

bool RayTraceGIPass::DrawRayTraceGI(FrameRenderData& data, RenderState& state)
{
	auto& target = data.originTexture;

	if (!target || target->IsEmpty())
		return false;

	GLfloat clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glClearTexImage(target->GetID(), 0, GL_RGBA, GL_FLOAT, clearColor);

	auto& rayTraceShader = _rayTraceShader_useGbuffer;
	rayTraceShader.Use();
	if (!BindGeneralData(rayTraceShader))
		return false;

	RenderHelp::SetupLightingData(
		rayTraceShader,
		state.lights.dirLightInfos,
		state.lights.pointLightInfos,
		state.lights.spotLightInfos,
		state.lights.shadowAtlas
	);

	rayTraceShader.setIVec2("screenSize", data.drawSize);

	//光追参数
	rayTraceShader.setFloat("tMin", std::max(0.f, state.option.rayTraceGIParams.tMin));
	rayTraceShader.setFloat("tMax", std::max(0.f, state.option.rayTraceGIParams.tMax));
	rayTraceShader.setInt("maxBounce", std::max(1, std::min(state.option.rayTraceGIParams.maxBounceLimit, OpenGLRenderConfig::RayTrace_Max_Bounce_limit)));

	rayTraceShader.setInt("sampleRayCount", std::max(1, state.option.rayTraceGIParams.NumSamples));
	rayTraceShader.setFloat("GIIntensity", std::max(0.01f, state.option.rayTraceGIParams.GIIntensity));
	rayTraceShader.setFloat("AOIntensity", std::max(0.01f, state.option.rayTraceGIParams.AOIntensity));
	rayTraceShader.setFloat("DistanceFactor", std::max(0.0001f, state.option.rayTraceGIParams.DistanceFactor));

	rayTraceShader.setInt("frameIndex", state.renderRecord.frameIndex % 100000);

	rayTraceShader.setTexture(data.gPosition, "gPosition", 5);
	rayTraceShader.setTexture(data.gNormal, "gNormal", 6);
	rayTraceShader.setTexture(data.gAlbedoOpacity, "gAlbedoOpacity", 7);
	rayTraceShader.setTexture(data.gMetallicRoughness, "gMetallicRoughness", 8);
	rayTraceShader.setTexture(data.sceneDepthBuffer, "depthMap", 9);

	rayTraceShader.setTexture(data.atlasShadowMap, "atlasShadowMap", 10);

	if (data.ssaoMap) rayTraceShader.setTexture(data.ssaoMap, "SSAOMap", 11);

	glBindImageTexture(0, target->GetID(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);

	glDispatchCompute((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);// 分发计算任务
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	return true;
}

bool RayTraceGIPass::DrawSpatialDenoising(FrameRenderData& data, RenderState& state)
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
	_spatialDenoisingShader.setTexture(data.sceneDepthBuffer, "depthMap", 10);

	_spatialDenoisingShader.setFloat("blurRadius", state.option.rayTraceGIParams.BlurRadius);
	_spatialDenoisingShader.setFloat("blurDepthWeight", state.option.rayTraceGIParams.BlurDepthWeight);
	_spatialDenoisingShader.setInt("kernelSize", state.option.rayTraceGIParams.BlurKernelSize);
	_spatialDenoisingShader.setFloat("sigma", state.option.rayTraceGIParams.BlurGaussSigma);

	glBindImageTexture(0, targetTex->GetID(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
	glDispatchCompute((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	return true;
}

bool RayTraceGIPass::DrawTemporalDenoising(FrameRenderData& data, RenderState& state)
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

bool RayTraceGIPass::DrawScale(FrameRenderData& data, RenderState& state)
{
	std::shared_ptr<Texture2D> srcTex;
	std::shared_ptr<Texture2D>& targetTex = data.outPutTexture;

	if (data.outPutTexture)
		srcTex = data.outPutTexture;
	else
		srcTex = data.originTexture;

	if (!srcTex || srcTex->IsEmpty())
		return false;

	int srcWidth = srcTex->GetWidth();
	int srcHeight = srcTex->GetHeight();

	if (!targetTex || targetTex->IsEmpty())
		return false;

	GLfloat clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glClearTexImage(targetTex->GetID(), 0, GL_RGBA, GL_FLOAT, clearColor);

	int destWidth = targetTex->GetWidth();
	int destHeight = targetTex->GetHeight();

	if (srcWidth == destWidth && srcHeight == destHeight)
	{
		Texture2D::CopyTexture(srcTex, targetTex);
	}
	else
	{
		_scaleShader.Use();

		_scaleShader.setIVec2("srcScreenSize", glm::ivec2(srcWidth, srcHeight));
		_scaleShader.setIVec2("destScreenSize", glm::ivec2(destWidth, destHeight));

		glBindImageTexture(0, srcTex->GetID(), 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, targetTex->GetID(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
		glDispatchCompute((destWidth + work_size_x - 1) / work_size_x, (destHeight + work_size_y - 1) / work_size_y, 1);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	}

	return true;
}

void RayTraceGIPass::SetEnable(bool enable) const
{
	if (_enable == enable)
		return;
	_enable = enable;
	if (_enable)
		_firstDrawTemporal = true;
}

bool RayTraceGIPass::BindGeneralData(Shader& shader)
{
	if (!_buffers)
		return false;

	auto bindSSBO = [&shader](const std::string& name, const std::shared_ptr<SSBO>& ssbo) -> bool {
		return ssbo && shader.bindSSBO(name, ssbo);
		};

	return bindSSBO("TriangleBuffer", _buffers->GetTraiangles())
		&& bindSSBO("TriangleExtBuffer", _buffers->GetTraiangleExt())
		&& bindSSBO("MeshMatDataBuffer", _buffers->GetMeshmatData())
		&& bindSSBO("WorldBVHNodeBuffer", _buffers->GetWorldBVHNode())
		&& bindSSBO("MeshBVHNodeBuffer", _buffers->GetMeshBVHNode());
}

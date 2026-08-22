#include "OpenGLRenderEngine/RenderPass/RayTraceReflectPass.h"

#define work_size_x 16
#define work_size_y 16

RayTraceReflectPass::RayTraceReflectPass(
	const std::string& rayTraceComputerShaderPath,
	const std::string& denoisedComputerShaderPath,
	const std::string& scaleComputerShaderPath
)
	:
	useDenoised(true)
{
	_rayTraceShader_useGbuffer.AddDefineMacro("useGbuffer", "");
	_rayTraceShader_useGbuffer.AddDefineMacro("Max_Recursive_Depth", OpenGLRenderConfig::RayTrace_Max_Recursive_Depth);
	_rayTraceShader_useGbuffer.AddDefineMacro("Max_Bounce_limit", OpenGLRenderConfig::RayTrace_Max_Bounce_limit);

	_rayTraceShader_useGbuffer.CompileFromFile(rayTraceComputerShaderPath);

	_denoisedShader.CompileFromFile(denoisedComputerShaderPath);
	_scaleShader.CompileFromFile(scaleComputerShaderPath);
}

RayTraceReflectPass::~RayTraceReflectPass()
{
}

bool RayTraceReflectPass::ShouldExecute(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!state.option.flags.rayTraceReflectOn)
		return false;
	return state.option.rayTraceReflectParams.maxBounceLimit > 0;
}

void RayTraceReflectPass::Execute(OpenGLRenderGraph::FrameDataRegistry& registry, const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{
	if (!ShouldExecute(registry, state))
		return;

	bool needDraw = state.option.rayTraceReflectParams.maxBounceLimit > 0;
	if (!needDraw)
		return;

	FrameRenderData data;
	data.scrSize = glm::ivec2(state.framebuffer.width, state.framebuffer.height);
	data.drawSize = data.scrSize;
	data.gPosition = ctx.GetInput(0);
	data.gNormal = ctx.GetInput(1);
	data.gAlbedoOpacity = ctx.GetInput(2);
	data.gMetallicRoughness = ctx.GetInput(3);
	data.atlasShadowMap = ctx.GetInput(4);

	data.sceneDepthBuffer = ctx.GetExternal(0);

	data.originTexture = ctx.GetTemp(0);
	data.denoisedTexture = ctx.GetTemp(1);

	data.historyColorTexture = ctx.GetPersitent(0);

	data.outPutTexture = ctx.GetOutput(0);

	if (needDraw && !DrawRayTrace(data, state)) return;

	if (needDraw && useDenoised && !DrawDenoised(data, state)) return;

	if (!DrawScale(data, state)) return;

	//RENDERCONTEXMANAGER->WithTempReleaseMainOpenGLBind([&]()->void {
	//	THREADCONTEXT->UnBind();
	//	auto task1 = CoroTask::Run([&]()-> void {DrawTexture(data.outPutTexture, "temp/test_RayTraceReflectPass.png"); });
	//	task1.sync_wait();
	//	THREADCONTEXT->Bind();
	//	});
}

void RayTraceReflectPass::FrameBegin(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	SetEnableDenoised(state.option.rayTraceReflectParams.useDenoised);
	if (!ShouldExecute(registry, state))
		return;
}

void RayTraceReflectPass::SetGeneralBuffer(std::shared_ptr<RayTraceGeneralBuffer> buffer) {
	_buffers = buffer;
}

bool RayTraceReflectPass::DrawRayTrace(FrameRenderData& data, RenderState& state)
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
	rayTraceShader.setFloat("tMin", std::max(0.f, state.option.rayTraceReflectParams.tMin));
	rayTraceShader.setFloat("tMax", std::max(0.f, state.option.rayTraceReflectParams.tMax));
	rayTraceShader.setInt("maxBounce", std::max(1, std::min(state.option.rayTraceReflectParams.maxBounceLimit, OpenGLRenderConfig::RayTrace_Max_Bounce_limit)));


	rayTraceShader.setTexture(data.gPosition, "gPosition", 5);
	rayTraceShader.setTexture(data.gNormal, "gNormal", 6);
	rayTraceShader.setTexture(data.gAlbedoOpacity, "gAlbedoOpacity", 7);
	rayTraceShader.setTexture(data.gMetallicRoughness, "gMetallicRoughness", 8);
	rayTraceShader.setTexture(data.sceneDepthBuffer, "depthMap", 9);

	rayTraceShader.setTexture(data.atlasShadowMap, "atlasShadowMap", 10);

	glBindImageTexture(0, target->GetID(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);

	glDispatchCompute((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);// 分发计算任务
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	return true;
}

bool RayTraceReflectPass::DrawDenoised(FrameRenderData& data, RenderState& state)
{
	std::shared_ptr<Texture2D> srcTex = data.originTexture;
	std::shared_ptr<Texture2D>& targetTex = data.denoisedTexture;

	if (!srcTex || srcTex->IsEmpty())
		return false;

	if (!targetTex || targetTex->IsEmpty())
		return false;

	GLfloat clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glClearTexImage(targetTex->GetID(), 0, GL_RGBA, GL_FLOAT, clearColor);

	_denoisedShader.Use();
	_denoisedShader.setIVec2("screenSize", data.drawSize);

	glBindImageTexture(0, srcTex->GetID(), 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
	glBindImageTexture(1, targetTex->GetID(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
	glDispatchCompute((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	return true;
}

bool RayTraceReflectPass::DrawScale(FrameRenderData& data, RenderState& state)
{
	std::shared_ptr<Texture2D> srcTex;
	std::shared_ptr<Texture2D>& targetTex = data.outPutTexture;

	if (useDenoised && data.denoisedTexture && !data.denoisedTexture->IsEmpty())
		srcTex = data.denoisedTexture;
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

void RayTraceReflectPass::SetEnableDenoised(bool enable)
{
	if (useDenoised == enable)
		return;

	useDenoised = enable;
}

bool RayTraceReflectPass::BindGeneralData(Shader& shader)
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
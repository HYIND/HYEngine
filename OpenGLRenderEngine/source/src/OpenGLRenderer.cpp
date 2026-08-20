#include "OpenGLRenderEngine/OpenGLRenderer.h"
#include "OpenGLRenderEngine/General/FBOHelper.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
#include "OpenGLRenderEngine/General/GPUTimer.h"
#include "OpenGLRenderEngine/OpenGLRenderConfig.h"

#include "OpenGLRenderEngine/RenderPass/GeometryPass.h"
#include "OpenGLRenderEngine/RenderPass/LightShadowDepthPass.h"
#include "OpenGLRenderEngine/RenderPass/LightingPass.h"
#include "OpenGLRenderEngine/RenderPass/LightDrawPass.h"
#include "OpenGLRenderEngine/RenderPass/SSRPass.h"
#include "OpenGLRenderEngine/RenderPass/SkyBoxPass.h"
#include "OpenGLRenderEngine/RenderPass/SSAOPass.h"
#include "OpenGLRenderEngine/RenderPass/EffectPass.h"
#include "OpenGLRenderEngine/RenderPass/RayTracePass.h"
#include "OpenGLRenderEngine/RenderPass/DepthFogPass.h"
#include "OpenGLRenderEngine/RenderPass/TransparentPass.h"
#include "OpenGLRenderEngine/RenderPass/AutoExposurePass.h"
#include "OpenGLRenderEngine/RenderPass/SSGIPass.h"
#include "OpenGLRenderEngine/RenderPass/HZBPass.h"
#include "OpenGLRenderEngine/RenderPass/PreCalculatePass.h"

#include "Helper/Tools.h"

OpenGLRenderer::OpenGLRenderer()
{
	scr_width = 1;
	scr_height = 1;
}

OpenGLRenderer::~OpenGLRenderer()
{
}

void OpenGLRenderer::Init(uint32_t width, uint32_t height, SharedTexture* sharedTexture)
{
	width = std::max(1u, width);
	height = std::max(1u, height);

	scr_width = width;
	scr_height = height;

	glewExperimental = GL_TRUE;

	//stbi_set_flip_vertically_on_load(true);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	//glEnable(GL_STENCIL_TEST);
	//glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
	//glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

	//glEnable(GL_BLEND);
	//glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glEnable(GL_CULL_FACE);
	//glCullFace(GL_FRONT);

	glEnable(GL_MULTISAMPLE);

	_firstPersonPass = std::make_unique<FirstPersonPass>("shader/FPS/firstperson.vs", "shader/FPS/firstperson.fs");

	_combinPass = std::make_unique<CombinPass>("shader/postprocess/combin.vs", "shader/postprocess/combin.fs", scr_width, scr_height);

	_globalBloomPass = std::make_unique<BloomPass>(std::string("shader/postprocess/bloomblur.vs"), std::string("shader/postprocess/bloomblur.fs"), scr_width, scr_height);
	_globalPostProcessPass = std::make_unique<GlobalPostProcessPass>("shader/postprocess/globalpostprocess.vs", "shader/postprocess/globalpostprocess.fs", scr_width, scr_height);

	FBOHelper::InitFbo(_renderTarget.sceneFbo, _renderTarget.sceneColorBuffer, _renderTarget.sceneDepthBuffer, scr_width, scr_height);
	FBOHelper::InitFbo(_renderTarget.firstPersonFbo, _renderTarget.firstPersonColorBuffer, _renderTarget.firstPersonDepthBuffer, scr_width, scr_height);
	FBOHelper::InitFbo(_renderTarget.combinFbo, _renderTarget.combinColorBuffer, _renderTarget.combinBrightColorBuffer, _renderTarget.combinDepthBuffer, scr_width, scr_height);

	if (!sharedTexture)
	{
		FBOHelper::InitFbo(_renderTarget.finalFbo, _renderTarget.finalColorBuffer, scr_width, scr_height);
		needFlipFinalFboY = false;
	}
	else
	{
		FBOHelper::InitFbo(_renderTarget.finalFbo, sharedTexture, scr_width, scr_height);
		_renderTarget.finalColorBuffer = sharedTexture->glTex;
		_renderTarget.sharedTexture = sharedTexture;
		needFlipFinalFboY = true;
	}


	glGenBuffers(1, &_cameraCache.curUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, _cameraCache.curUBO);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(comp_camera), nullptr, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, _cameraCache.curUBO);  // 绑定到 0

	glGenBuffers(1, &_cameraCache.prevUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, _cameraCache.prevUBO);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(comp_camera), nullptr, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, 2, _cameraCache.prevUBO);  // 绑定到 2

	InitRenderGraph();
}

void OpenGLRenderer::Draw(RenderState& state)
{
	SetupRenderState(state);
	//SetupIndirectDrawData(state);

	_sceneRenderGraph->Execute(state);
	//_firstPersonRenderGraph->Execute(state);

	//RenderFirstPersonLayer(state);

	static bool draw = false;
	if (draw)
		DrawTexture(_renderTarget.sceneColorBuffer, "temp/color.png");

	_combinPass->Draw(_renderTarget.combinFbo, { _renderTarget.sceneColorBuffer->GetID() ,_renderTarget.firstPersonColorBuffer->GetID() });

	auto& option = state.option;
	if (option.flags.bloomOn) _globalBloomPass->Draw(_renderTarget.combinBrightColorBuffer->GetID());
	_globalPostProcessPass->Draw(
		_renderTarget.finalFbo, _renderTarget.combinColorBuffer->GetID(), _globalBloomPass->GetBloomBlurMap(),
		option.flags.bloomOn, option.flags.gammaOn, needFlipFinalFboY,
		pow(2.0f, option.postProcessParams.EV100), option.postProcessParams.gamma
	);

	FinishRendering(state);
}

GLuint OpenGLRenderer::GetColorBuffer() const
{
	return _renderTarget.finalColorBuffer;
}

GLuint OpenGLRenderer::GetColorFBO() const
{
	return _renderTarget.finalFbo;
}

int OpenGLRenderer::GetWidth() const
{
	return scr_width;
}

int OpenGLRenderer::GetHeight() const
{
	return scr_height;
}

RenderOption OpenGLRenderer::GetOption()
{
	return _option;
}

void OpenGLRenderer::SetOption(RenderOption option)
{
	_option = option;
}

void OpenGLRenderer::Resize(uint32_t width, uint32_t height)
{
	width = std::max(1u, width);
	height = std::max(1u, height);
	if (scr_width == width && scr_height == height)
		return;

	scr_width = width;
	scr_height = height;

	_combinPass->OnResize(scr_width, scr_height);
	_globalBloomPass->OnResize(scr_width, scr_height);
	_globalPostProcessPass->OnResize(scr_width, scr_height);

	FBOHelper::InitFbo(_renderTarget.sceneFbo, _renderTarget.sceneColorBuffer, _renderTarget.sceneDepthBuffer, scr_width, scr_height);
	FBOHelper::InitFbo(_renderTarget.firstPersonFbo, _renderTarget.firstPersonColorBuffer, _renderTarget.firstPersonDepthBuffer, scr_width, scr_height);
	FBOHelper::InitFbo(_renderTarget.combinFbo, _renderTarget.combinColorBuffer, _renderTarget.combinBrightColorBuffer, _renderTarget.combinDepthBuffer, scr_width, scr_height);

	if (!_renderTarget.sharedTexture)
	{
		FBOHelper::InitFbo(_renderTarget.finalFbo, _renderTarget.finalColorBuffer, scr_width, scr_height);
		needFlipFinalFboY = false;
	}
	else
	{
		FBOHelper::InitFbo(_renderTarget.finalFbo, _renderTarget.sharedTexture, scr_width, scr_height);
		_renderTarget.finalColorBuffer = _renderTarget.sharedTexture->glTex;
		needFlipFinalFboY = true;
	}

	InitRenderGraph();
}

void OpenGLRenderer::InitRenderGraph()
{
	InitSceneRenderGraph();
	InitFirstPersonRenderGraph();
}

// RenderGraphResource生成器，输入描述信息或者参照物（如其他res，已有的Texture2D），获取资源声明
//
class ResourceBuilder
{
public:
	ResourceBuilder(std::unique_ptr<OpenGLRenderGraph::RenderGraph>& graph) :_graph(graph) {}

public:
	OpenGLRenderGraph::RenderGraphResource CreateTexture(
		int width, int height,
		GLenum format,
		GLenum minFilterMode, GLenum magFilterMode,
		GLenum wrapSMode, GLenum wrapTMode,
		const OpenGLRenderGraph::ResourceName& name,
		uint32_t maxLevel = 1) {
		return _graph->CreateTexture(OpenGLRenderGraph::TextureDesc{ width ,height,format,minFilterMode,magFilterMode,wrapSMode,wrapTMode,std::max(1u,maxLevel) }, name);
	}
	OpenGLRenderGraph::RenderGraphResource CreateTexture(int width, int height, GLenum format, GLenum filterMode, GLenum wrapMode, const OpenGLRenderGraph::ResourceName& name, uint32_t maxLevel = 1) {
		return _graph->CreateTexture(OpenGLRenderGraph::TextureDesc{ width ,height,format,filterMode,filterMode,wrapMode,wrapMode,std::max(1u,maxLevel) }, name);
	}
	OpenGLRenderGraph::RenderGraphResource CreateTexture(const OpenGLRenderGraph::RenderGraphResource& other, const OpenGLRenderGraph::ResourceName& name) {
		return _graph->CreateTexture(std::get<OpenGLRenderGraph::TextureDesc>(other.desc), name);
	}
	OpenGLRenderGraph::RenderGraphResource CreateTexture(const std::shared_ptr<Texture2D>& tex, const OpenGLRenderGraph::ResourceName& name) {
		return CreateTexture(tex->GetWidth(), tex->GetHeight(), tex->GetInternalFormat(), tex->GetMinFilter(), tex->GetMagFilter(), tex->GetWrapS(), tex->GetWrapT(), name, tex->GetMaxLevel());
	}
	OpenGLRenderGraph::ExternalResource CreateExternalTxture(const OpenGLRenderGraph::ResourceName& name) {
		return _graph->CreateExternalTexture(name);
	}

private:
	std::unique_ptr<OpenGLRenderGraph::RenderGraph>& _graph;
};

void OpenGLRenderer::InitSceneRenderGraph()
{
	int width = scr_width;
	int height = scr_height;

	_sceneRenderGraph = std::make_unique<OpenGLRenderGraph::RenderGraph>("SceneRenderGraph");

	// 注入RenderTarget
	_sceneRenderGraph->SetRenderTargetFBO(_renderTarget.sceneFbo);
	_sceneRenderGraph->InjectExternalTexture("renderTargetColorBuffer", _renderTarget.sceneColorBuffer);
	_sceneRenderGraph->InjectExternalTexture("renderTargetDepthBuffer", _renderTarget.sceneDepthBuffer);

	ResourceBuilder resbuilder(_sceneRenderGraph);

	// 获取外部注入资源的声明
	auto Ext_RenderTargetColorBuffer = resbuilder.CreateExternalTxture("renderTargetColorBuffer");
	auto Ext_RenderTargetDepthBuffer = resbuilder.CreateExternalTxture("renderTargetDepthBuffer");

	auto gPosition = resbuilder.CreateTexture(width, height, GL_RGB32F, GL_NEAREST, GL_CLAMP_TO_EDGE, "gPosition");
	auto gNormal = resbuilder.CreateTexture(width, height, GL_RGB16F, GL_NEAREST, GL_CLAMP_TO_EDGE, "gNormal");
	auto gAlbedoOpacity = resbuilder.CreateTexture(width, height, GL_RGBA, GL_NEAREST, GL_CLAMP_TO_EDGE, "gAlbedoOpacity");
	auto gMetallicRoughnessMap = resbuilder.CreateTexture(width, height, GL_RGBA16F, GL_NEAREST, GL_CLAMP_TO_EDGE, "gMetallicRoughnessMap");
	auto gMotionVectorMap = resbuilder.CreateTexture(width, height, GL_RG16F, GL_NEAREST, GL_CLAMP_TO_EDGE, "gMotionVectorMap");
	auto gDepthStencilMap = resbuilder.CreateTexture(_renderTarget.sceneDepthBuffer, "geometryPass_TempDepthStencilMap");
	auto ssaoOutPut = resbuilder.CreateTexture(width, height, GL_RED, GL_NEAREST, GL_CLAMP_TO_EDGE, "ssaoOutPutBuffer");
	auto rayTrace_Output = resbuilder.CreateTexture(width, height, GL_RGBA16F, GL_NEAREST, GL_CLAMP_TO_EDGE, "rayTrace_Output");
	auto ssr_Output = resbuilder.CreateTexture(width, height, GL_RGBA16F, GL_NEAREST, GL_CLAMP_TO_EDGE, "ssr_Output");
	auto ssgi_Output = resbuilder.CreateTexture(width, height, GL_RGBA16F, GL_NEAREST, GL_CLAMP_TO_EDGE, "ssgi_Output");
	auto atlasShadowMap = resbuilder.CreateTexture(1024, 1024, GL_DEPTH_COMPONENT, GL_LINEAR, GL_CLAMP_TO_EDGE, "atlasShadowMap");

	// 不透明物体
	auto preCalculatePass = _sceneRenderGraph->AddPass("preCalculatePass");
	auto hzbPass = _sceneRenderGraph->AddPass("hzbPass");
	auto geometryPass = _sceneRenderGraph->AddPass("geometry");
	auto lightingShadowDepthPass = _sceneRenderGraph->AddPass("lightingShadowDepth");
	auto ssaoPass = _sceneRenderGraph->AddPass("ssaoPass");
	auto lightingPass = _sceneRenderGraph->AddPass("lightingPass");
	auto copyDepthPass = _sceneRenderGraph->AddPass("copyDepthPass");
	auto skyBoxPass = _sceneRenderGraph->AddPass("skyBoxPass");
	auto rayTracePass = _sceneRenderGraph->AddPass("rayTracePass");
	auto ssrPass = _sceneRenderGraph->AddPass("ssrPass");
	auto ssgiPass = _sceneRenderGraph->AddPass("ssgiPass");
	auto combinIndirectLightingPass = _sceneRenderGraph->AddPass("combinIndirectLightingPass");
	auto lightDrawPass = _sceneRenderGraph->AddPass("lightDrawPass");
	auto opaqueFrence = _sceneRenderGraph->AddFence("opaqueFrence");

	// 透明物体
	auto effectPass = _sceneRenderGraph->AddPass("effectPass");
	auto transparentPass = _sceneRenderGraph->AddPass("transparentPass");
	auto transprantFrence = _sceneRenderGraph->AddFence("transprantFrence");
	transprantFrence->After(opaqueFrence);

	// 后处理
	auto depthFogPass = _sceneRenderGraph->AddPass("depthFogPass");
	auto postProcessFrence = _sceneRenderGraph->AddFence("postProcessFrence");
	postProcessFrence->After(transprantFrence);

	// 曝光计算
	auto autoExposurePass = _sceneRenderGraph->AddPass("autoExposurePass");

	auto postCalculatePass = _sceneRenderGraph->AddPass("postCalculatePass");

	preCalculatePass->SetRenderPass(std::make_unique<PreCalculatePass>());

	auto hzbRender = std::make_unique<HZBPass>(
		"shader/HZB/depth.vs",
		"shader/HZB/depth.fs",
		"shader/HZB/HZBGenerate.comp",
		"shader/HZB/occlusionCulling.comp"
	);
	auto hzbMap = resbuilder.CreateTexture(width, height, GL_R32F, GL_NEAREST, GL_CLAMP_TO_EDGE, "HZBMap", hzbRender->GetMaxLevel());

	hzbPass->SetRenderPass(std::move(hzbRender))
		.Temp(resbuilder.CreateTexture(width, height, GL_DEPTH_COMPONENT32F, GL_NEAREST, GL_CLAMP_TO_EDGE, "hzbPass_temp"))
		.Output(hzbMap)
		.After(preCalculatePass)
		.Before(geometryPass);

	lightingShadowDepthPass->SetRenderPass(std::make_unique<LightShadowDepthPass>(
		"shader/lighting/dirlightshadow.vs", "shader/lighting/dirlightshadow.gs", "shader/lighting/dirlightshadow.fs",
		"shader/lighting/pointlightshadow.vs", "shader/lighting/pointlightshadow.gs", "shader/lighting/pointlightshadow.fs"))
		.Persistent(atlasShadowMap)
		.After(preCalculatePass)
		.Before(lightingPass, postCalculatePass);

	geometryPass->SetRenderPass(std::make_unique<GeometryPass>(
		"shader/gbuffer/geometrypass_StaticMesh.vs",
		"shader/gbuffer/geometrypass_StaticMesh.fs",
		"shader/gbuffer/geometrypass_SkinnedMesh.vs",
		"shader/gbuffer/geometrypass_SkinnedMesh.fs"))
		.Output(gPosition, gNormal, gAlbedoOpacity, gMetallicRoughnessMap, gMotionVectorMap, gDepthStencilMap)
		.After(hzbPass, preCalculatePass)
		.Before(lightingPass, postCalculatePass);

	ssaoPass->SetRenderPass(std::make_unique<SSAOPass>("shader/ssao/ssao.vs", "shader/ssao/ssao.fs", "shader/ssao/ssao.vs", "shader/ssao/ssaoblur.fs"));
	ssaoPass->Input(gPosition, gNormal)
		.Temp(resbuilder.CreateTexture(ssaoOutPut, "ssaoColorBuffer"))
		.Output(ssaoOutPut)
		.After(geometryPass)
		.Before(lightingPass);

	lightingPass->SetRenderPass(std::make_unique<LightingPass>("shader/lighting/lightingpass.vs", "shader/lighting/lightingpass.fs"))
		.Input(gPosition, gNormal, gAlbedoOpacity, gMetallicRoughnessMap, atlasShadowMap, ssaoOutPut)
		.After(lightingShadowDepthPass, ssaoPass)
		.Before(opaqueFrence);

	copyDepthPass->SetRenderPass(MakeLambdaPass([](const OpenGLRenderGraph::PassContext& ctx, RenderState& state)-> void
		{
			auto geometryDepthStencil = ctx.GetInput(0);
			auto renderTargetDepthBuffer = ctx.GetExternal(0);
			Texture2D::CopyTexture(geometryDepthStencil, renderTargetDepthBuffer);
		}))
		.Input(gDepthStencilMap)
		.External(Ext_RenderTargetDepthBuffer)
		.After(lightingPass)
		.Before(skyBoxPass, opaqueFrence);

	skyBoxPass->SetRenderPass(std::make_unique<SkyBoxPass>("shader/skybox/skybox.vs", "shader/skybox/skybox.fs"))
		.After(copyDepthPass)
		.Before(opaqueFrence);

	rayTracePass->SetRenderPass(std::make_unique<RayTracePass>("shader/RayTrace/rayTrace.comp", "shader/RayTrace/TAAMix.comp", "shader/General/imagedenoising.comp", "shader/General/imagescale.comp"))
		.Input(gPosition, gNormal, gAlbedoOpacity, gMetallicRoughnessMap, atlasShadowMap)
		.Temp(resbuilder.CreateTexture(rayTrace_Output, "rayTrace_TempOrigin")
			, resbuilder.CreateTexture(rayTrace_Output, "rayTrace_TempDenoised"))
		.External(Ext_RenderTargetColorBuffer, Ext_RenderTargetDepthBuffer)
		.Output(rayTrace_Output)
		.Persistent(resbuilder.CreateTexture(rayTrace_Output, "rayTrace_TAAPingPongTexture[0]")
			, resbuilder.CreateTexture(rayTrace_Output, "rayTrace_TAAPingPongTexture[1]"))
		.After(copyDepthPass, skyBoxPass, lightingPass)
		.Before(opaqueFrence);

	ssrPass->SetRenderPass(std::make_unique<SSRPass>("shader/ssr/SSReflect.comp", "shader/ssr/BilateralFilterBlur.comp"))
		.Input(gPosition, gNormal, gAlbedoOpacity, gMetallicRoughnessMap)
		.Temp(resbuilder.CreateTexture(ssr_Output, "ssrPass_temp1"))
		.External(Ext_RenderTargetColorBuffer, Ext_RenderTargetDepthBuffer)
		.Output(ssr_Output)
		.After(copyDepthPass, skyBoxPass, lightingPass)
		.Before(opaqueFrence);

	ssgiPass->SetRenderPass(std::make_unique<SSGIPass>("shader/ssr/SSGI.comp", "shader/ssr/BilateralFilterBlur.comp", "shader/ssr/TemporalAccumulate.comp"))
		.Input(gPosition, gNormal, gAlbedoOpacity, gMetallicRoughnessMap, gMotionVectorMap, ssaoOutPut, hzbMap)
		.Temp(resbuilder.CreateTexture(ssgi_Output, "ssgiPass_temp1"), resbuilder.CreateTexture(ssgi_Output, "ssgiPass_temp2"))
		.Persistent(resbuilder.CreateTexture(ssgi_Output, "ssgiPass_historyColorTexture"))
		.External(Ext_RenderTargetColorBuffer, Ext_RenderTargetDepthBuffer)
		.Output(ssgi_Output)
		.After(copyDepthPass, skyBoxPass, lightingPass)
		.Before(opaqueFrence);

	std::shared_ptr<Shader> shader = std::make_shared<Shader>();
	shader->AddDefineMacro("COMBIN_MODE", 1);
	shader->CompileFromFile("shader/postprocess/combin.vs", "shader/postprocess/combin.fs");
	combinIndirectLightingPass->SetRenderPass(MakeLambdaPass([_shader = std::move(shader)](const OpenGLRenderGraph::PassContext& ctx, RenderState& state) mutable -> void
		{
			if (!_shader)
				return;

			std::vector<std::shared_ptr<Texture2D>> all_tex;
			for (auto& textures : { ctx.inputTextures, ctx.optionInputTextures }) {
				for (auto& tex : textures) {
					if (tex) all_tex.push_back(tex);
				}
			}
			if (all_tex.empty())
				return;

			auto sceneColorBuffer = ctx.GetExternal(0);
			if (!sceneColorBuffer)
				return;

			auto tempColorBuffer = ctx.GetTemp(0);
			if (!Texture2D::CopyTexture(sceneColorBuffer, tempColorBuffer))
				return;

			all_tex.push_back(tempColorBuffer);
			//for (int i = 0; i < all_tex.size(); i++)
				//DrawTexture(all_tex[i], std::format("temp/all_tex{}.png", i));

			glBindFramebuffer(GL_FRAMEBUFFER, ctx.renderTargetFBO);
			glDepthMask(GL_FALSE);
			glViewport(0, 0, state.framebuffer.width, state.framebuffer.height);

			_shader->Use();
			int count = std::min(Max_Color_Buffer_Count, (int)all_tex.size());
			for (int i = 0; i < all_tex.size(); i++)
			{
				std::string name = std::format("ColorMap[{}]", i);
				_shader->setTexture(all_tex[i], name, i);
			}
			_shader->setInt("ColorMapCount", count);

			RenderHelp::renderScreenQuad();
			glDepthMask(GL_TRUE);
		}))
		.InputOption(rayTrace_Output, ssr_Output, ssgi_Output)
		.External(Ext_RenderTargetColorBuffer, Ext_RenderTargetColorBuffer)
		.Temp(resbuilder.CreateTexture(_renderTarget.sceneColorBuffer, "combinIndirectLightingPass_temp1"))
		.After(rayTracePass, ssrPass, ssgiPass)
		.Before(opaqueFrence);

	lightDrawPass->SetRenderPass(std::make_unique<LightDrawPass>("shader/lighting/lightMesh.vs", "shader/lighting/lightMesh.fs"))
		.After(combinIndirectLightingPass)
		.Before(opaqueFrence);

	effectPass->SetRenderPass(std::make_unique<EffectPass>("shader/effect/effectpass.vs", "shader/effect/effectpass.fs"))
		.After(opaqueFrence)
		.Before(transprantFrence);

	transparentPass->SetRenderPass(std::make_unique<TransparentPass>("shader/Transparent/transparentpass.vs", "shader/Transparent/transparentpass.fs"))
		.Input(atlasShadowMap)
		.After(opaqueFrence, effectPass)
		.Before(transprantFrence);

	depthFogPass->SetRenderPass(std::make_unique<DepthFogPass>("shader/postprocess/depthFog.vs", "shader/postprocess/depthFog.fs"))
		.After(opaqueFrence, transprantFrence)
		.External(Ext_RenderTargetColorBuffer, Ext_RenderTargetDepthBuffer)
		.Temp(resbuilder.CreateTexture(_renderTarget.sceneColorBuffer, "depthFogPass_TempColor"),
			resbuilder.CreateTexture(_renderTarget.sceneDepthBuffer, "depthFogPass_TempDepth"))
		.Before(postProcessFrence);

	autoExposurePass->SetRenderPass(std::make_unique<AutoExposurePass>("shader/AutoExposure/histogram.comp"))
		.After(postProcessFrence)
		.External(Ext_RenderTargetColorBuffer);

	_sceneRenderGraph->Compile();
}

void OpenGLRenderer::InitFirstPersonRenderGraph()
{
	int width = scr_width;
	int height = scr_height;

	_firstPersonRenderGraph = std::make_unique<OpenGLRenderGraph::RenderGraph>("FirstPersonRenderGraph");

}

void OpenGLRenderer::EarlyProcess(RenderState& state)
{
	state.renderRecord.frameIndex = _record.frameIndex++;
	state.option = _option;

	_sceneRenderGraph->EarlyExecute(state);
	//_firstPersonRenderGraph->EarlyExecute(state);
}

void OpenGLRenderer::SetupRenderState(RenderState& state)
{
	glBindFramebuffer(GL_FRAMEBUFFER, _renderTarget.sceneFbo);
	glViewport(0, 0, scr_width, scr_height);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	glBindBuffer(GL_UNIFORM_BUFFER, _cameraCache.prevUBO);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(comp_camera), &_cameraCache.data);

	_cameraCache.data.projection = state.camera.projection;
	_cameraCache.data.view = state.camera.view;
	_cameraCache.data.projView = _cameraCache.data.projection * _cameraCache.data.view;
	_cameraCache.data.invView = glm::inverse(state.camera.view);
	glm::mat4 invTrans = glm::mat3(glm::transpose(_cameraCache.data.invView));
	_cameraCache.data.invTransViewRow1 = invTrans[0];
	_cameraCache.data.invTransViewRow2 = invTrans[1];
	_cameraCache.data.invTransViewRow3 = invTrans[2];
	_cameraCache.data.position = state.camera.position;
	_cameraCache.data.direction = state.camera.direction;
	_cameraCache.data.directionUp = state.camera.directionUp;
	_cameraCache.data.directionRight = state.camera.directionRight;
	_cameraCache.data.nearPlane = state.camera.nearPlane;
	_cameraCache.data.farPlane = state.camera.farPlane;
	_cameraCache.data.fov = state.camera.fov;
	glBindBuffer(GL_UNIFORM_BUFFER, _cameraCache.curUBO);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(comp_camera), &_cameraCache.data);

	state.framebuffer.width = scr_width;
	state.framebuffer.height = scr_height;

	state.renderRecord.prevEV100 = _record.prevEV100;
	state.renderRecord.prevRenderMicroTimeStamp = _record.prevRenderMicroTimeStamp;
	state.renderRecord.currentRenderMicroTimeStamp = Tool::GetTimestampMircoseconds();

}

void OpenGLRenderer::SetupIndirectDrawData(RenderState& state)
{

	auto indirectManager = IndirectDrawManager::Instance();

	{
		auto& items = state.objects.sceneRenderData.opaqueMesh;
		for (auto& item : items)
		{
			auto& mesh = item.meshinfo.mesh;
			if (mesh->GetNeedUpdateIndricetDraw())
			{
				indirectManager->setupMesh(*mesh);
				mesh->SetNeedUpdateIndirectDraw(false);
			}
		}
	}

	{
		auto& items = state.objects.sceneRenderData.transparentMesh;
		for (auto& item : items)
		{
			auto& mesh = item.meshinfo.mesh;
			if (mesh->GetNeedUpdateIndricetDraw())
			{
				indirectManager->setupMesh(*mesh);
				mesh->SetNeedUpdateIndirectDraw(false);
			}
		}
	}

	{
		auto& items = state.objects.sceneRenderData.opaqueSkinnedModel;
		for (auto& item : items)
		{
			for (auto& info : item.models)
			{
				auto& mesh = info.mesh;
				if (mesh->GetNeedUpdateIndricetDraw())
				{
					indirectManager->setupMesh(*mesh);
					mesh->SetNeedUpdateIndirectDraw(false);
				}
			}
		}
	}

	{
		auto& items = state.objects.sceneRenderData.transparentSkinnedMesh;
		for (auto& item : items)
		{
			auto& mesh = item.meshinfo.mesh;
			if (mesh->GetNeedUpdateIndricetDraw())
			{
				indirectManager->setupMesh(*mesh);
				mesh->SetNeedUpdateIndirectDraw(false);
			}
		}
	}
}

void OpenGLRenderer::FinishRendering(RenderState& state)
{
	_record.prevEV100 = state.option.postProcessParams.EV100;
	_record.prevRenderMicroTimeStamp = state.renderRecord.currentRenderMicroTimeStamp;
}

void OpenGLRenderer::RenderFirstPersonLayer(RenderState& state)
{
	_firstPersonPass->Draw(state);
}

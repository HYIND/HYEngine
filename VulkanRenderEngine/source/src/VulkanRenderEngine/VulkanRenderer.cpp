#include "vkstdafx.h"
#include "VulkanRenderEngine/VulkanRenderer.h"

#include "VulkanRenderEngine/Base/GraphicsPipeline.h"

#include "VulkanRenderEngine/General/FBOHelper.h"

#include "VulkanRenderEngine/RenderPass/GeometryPass.h"
#include "VulkanRenderEngine/RenderPass/LightShadowDepthPass.h"
#include "VulkanRenderEngine/RenderPass/LightingPass.h"
#include "VulkanRenderEngine/RenderPass/LightDrawPass.h"
#include "VulkanRenderEngine/RenderPass/SSRPass.h"
#include "VulkanRenderEngine/RenderPass/SkyBoxPass.h"
#include "VulkanRenderEngine/RenderPass/SSAOPass.h"
//#include "VulkanRenderEngine/RenderPass/EffectPass.h"
#include "VulkanRenderEngine/RenderPass/RayTraceGeneralPass.h"
#include "VulkanRenderEngine/RenderPass/RayTraceReflectPass.h"
#include "VulkanRenderEngine/RenderPass/RayTraceGIPass.h"
#include "VulkanRenderEngine/RenderPass/DepthFogPass.h"
//#include "VulkanRenderEngine/RenderPass/TransparentPass.h"
#include "VulkanRenderEngine/RenderPass/AutoExposurePass.h"
#include "VulkanRenderEngine/RenderPass/SSGIPass.h"
#include "VulkanRenderEngine/RenderPass/HZBPass.h"
#include "VulkanRenderEngine/RenderPass/PreCalculatePass.h"

static void NeedVulkanBaseInitlized()
{
	static std::once_flag flag;
	std::call_once(flag, []() {
		vk::Result result = (vk::Result)volkInitialize();
		if (result != vk::Result::eSuccess) {
			std::string str = std::format("volkInitialize failed: {}\n", to_string(result));
			std::cerr << str;
			throw std::runtime_error(str);
		}
		VULKAN_HPP_DEFAULT_DISPATCHER.init();
		VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
		});
}


// 提供扩展，创建vk实例，实例用于初始化VulkanRenderer
std::shared_ptr<VKCore::VulkanInstance> VulkanRenderer::CreateInstance(const std::vector<std::string>& extensionNames)
{
	NeedVulkanBaseInitlized();
	auto instance = std::make_shared<VKCore::VulkanInstance>();
	for (auto& name : extensionNames)
		instance->AddInstanceExtension(name);

	instance->SetLatestApiVersion();
	if (instance->CreateInstance() != vk::Result::eSuccess)
		return nullptr;
	return instance;
}

std::shared_ptr<VKCore::VulkanInstance> VulkanRenderer::CreateInstance(uint32_t extensionCount, const char** extensionNames)
{
	std::vector<std::string> temp;
	for (size_t i = 0; i < extensionCount; i++)
		temp.push_back(extensionNames[i]);

	return CreateInstance(temp);
}

std::shared_ptr<VulkanRenderer> VulkanRenderer::CreateForWindow(std::shared_ptr<VKCore::VulkanInstance> instance, VkSurfaceKHR surface, uint32_t width, uint32_t height, bool limitFrameRate)
{
	if (!instance) return nullptr;

	VKCONTEXT->SetInstance(instance);

	VKCore::VulkanPhysicalDeviceInfo info;
	if (!instance->GetSuitablePhysicalDevice(info, true, true, true, surface))
	{
		std::cout << std::format("[ CreateForWindow ] ERROR\nFailed to get suitable physical device\n");
		return nullptr;
	}

	auto vulkanSurface = std::make_shared<VKCore::VulkanSurface>(instance, surface);
	auto vulkanDevice = std::make_shared<VKCore::VulkanDevice>();
	vulkanDevice->AddDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	vulkanDevice->AddDeviceExtension(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
	vulkanDevice->AddDeviceExtension(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
	vulkanDevice->AddDeviceExtension(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
	vulkanDevice->AddDeviceExtension(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);
	vulkanDevice->AddDeviceExtension(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);

	if (vulkanDevice->Create(instance, info) != vk::Result::eSuccess)
		return nullptr;

	VKCONTEXT->SetDevice(vulkanDevice);

	auto vulkanSwapchain = std::make_shared<VKCore::VulkanSwapchain>();

	if (auto result = vulkanSwapchain->Create(vulkanDevice, vulkanSurface, { width, height }, limitFrameRate); result != vk::Result::eSuccess)
		return nullptr;

	auto renderer = std::make_shared<VulkanRenderer>();
	renderer->_vulkanInstance = instance;
	renderer->_vulkanSurface = vulkanSurface;
	renderer->_vulkanDevice = vulkanDevice;
	renderer->_vulkanSwapchain = vulkanSwapchain;
	renderer->InitForWindow(width, height);

	return renderer;
}

std::shared_ptr<VulkanRenderer> VulkanRenderer::CreateForSharedTexture(std::shared_ptr<VKCore::VulkanInstance> instance, std::shared_ptr<SharedTexture> sharedTexture)
{
	if (!instance || !sharedTexture || !sharedTexture->d3dTexture) return nullptr;

	uint32_t width = sharedTexture->width;
	uint32_t height = sharedTexture->height;

	VKCONTEXT->SetInstance(instance);

	VKCore::VulkanPhysicalDeviceInfo info;
	if (!instance->GetSuitablePhysicalDevice(info, true, true))
	{
		std::cout << std::format("[ CreateForOffscreen ] ERROR\nFailed to get suitable physical device\n");
		return nullptr;
	}

	auto vulkanDevice = std::make_shared<VKCore::VulkanDevice>();
	vulkanDevice->AddDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	vulkanDevice->AddDeviceExtension(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
	vulkanDevice->AddDeviceExtension(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
	vulkanDevice->AddDeviceExtension(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
	vulkanDevice->AddDeviceExtension(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);
	vulkanDevice->AddDeviceExtension(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);

	if (vulkanDevice->Create(instance, info) != vk::Result::eSuccess)
		return nullptr;

	VKCONTEXT->SetDevice(vulkanDevice);

	auto renderer = std::make_shared<VulkanRenderer>();
	renderer->_vulkanInstance = instance;
	renderer->_vulkanDevice = vulkanDevice;
	renderer->InitForSharedTexture(sharedTexture);

	return renderer;
}

std::shared_ptr<VulkanRenderer> VulkanRenderer::CreateForOffScreen(std::shared_ptr<VKCore::VulkanInstance> instance, uint32_t width, uint32_t height)
{
	if (!instance) return nullptr;

	VKCONTEXT->SetInstance(instance);

	VKCore::VulkanPhysicalDeviceInfo info;
	if (!instance->GetSuitablePhysicalDevice(info, true, true))
	{
		std::cout << std::format("[ CreateForOffscreen ] ERROR\nFailed to get suitable physical device\n");
		return nullptr;
	}

	auto vulkanDevice = std::make_shared<VKCore::VulkanDevice>();
	vulkanDevice->AddDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	vulkanDevice->AddDeviceExtension(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
	vulkanDevice->AddDeviceExtension(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
	vulkanDevice->AddDeviceExtension(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
	vulkanDevice->AddDeviceExtension(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);

	if (vulkanDevice->Create(instance, info) != vk::Result::eSuccess)
		return nullptr;

	VKCONTEXT->SetDevice(vulkanDevice);

	auto renderer = std::make_shared<VulkanRenderer>();
	renderer->_vulkanInstance = instance;
	renderer->_vulkanDevice = vulkanDevice;
	renderer->InitForOffSceen(width, height);

	return renderer;
}

VulkanRenderer::VulkanRenderer()
{
	scr_width = 1;
	scr_height = 1;
}

VulkanRenderer::~VulkanRenderer()
{}

void VulkanRenderer::InitForWindow(uint32_t width, uint32_t height)
{
	width = std::max(1u, width);
	height = std::max(1u, height);

	scr_width = width;
	scr_height = height;

	_mode = RenderMode::Present;

	Init_Internal();

	const Texture2DConfig config
	{
		.minFilter = vk::Filter::eLinear,
		.magFilter = vk::Filter::eLinear,
		.wrapU = vk::SamplerAddressMode::eClampToEdge,
		.wrapV = vk::SamplerAddressMode::eClampToEdge,
		.anisotropy = false,
		.gammaCorrection = false
	};
	_renderTarget.finalColorBuffer = std::make_shared<Texture2D>(scr_width, scr_height, vk::Format::eR8G8B8A8Unorm, config);


	auto imageCount = _vulkanSwapchain->GetSwapchainImageCount();

	_depthImages.clear();
	auto cmd = VKCONTEXT->GetCommandBuffer();
	cmd->Begin();
	for (size_t i = 0; i < imageCount; i++) {
		auto depthTex = std::make_shared<Texture2D>(scr_width, scr_height, vk::Format::eD24UnormS8Uint);
		depthTex->TransitionLayout(cmd, nullptr, Texture2D::BindStage::Graphics, Texture2D::BindUsage::Output);
		_depthImages.push_back(depthTex);
	}
	cmd->End();
	VKCONTEXT->SubmitCommandBufferToPendingQueue(cmd);

	_fence = std::make_shared<VKWrapper::VKFence>(_vulkanDevice.get());

	for (int i = 0; i < imageCount; i++)
	{
		_imageAcquiredSemaphores.push_back(std::move(std::make_shared<VKWrapper::VKSemaphore>(_vulkanDevice.get())));
		_renderFinishedSemaphores.push_back(std::move(std::make_shared<VKWrapper::VKSemaphore>(_vulkanDevice.get())));
	}

	InitRenderGraph();
}

void VulkanRenderer::InitForSharedTexture(std::shared_ptr<SharedTexture> sharedTexture)
{
	if (!sharedTexture)
		return;

	scr_width = sharedTexture->width;
	scr_height = sharedTexture->height;

	_mode = RenderMode::SharedTexture;

	Init_Internal();

	_renderTarget.finalColorBuffer = std::make_shared<Texture2D>(sharedTexture);

	InitRenderGraph();
}

void VulkanRenderer::InitForOffSceen(uint32_t width, uint32_t height)
{
	scr_width = width;
	scr_height = height;

	_mode = RenderMode::Offscreen;

	Init_Internal();

	const Texture2DConfig config
	{
		.minFilter = vk::Filter::eLinear,
		.magFilter = vk::Filter::eLinear,
		.wrapU = vk::SamplerAddressMode::eClampToEdge,
		.wrapV = vk::SamplerAddressMode::eClampToEdge,
		.anisotropy = false,
		.gammaCorrection = false
	};
	_renderTarget.finalColorBuffer = std::make_shared<Texture2D>(scr_width, scr_height, vk::Format::eR8G8B8A8Unorm, config);

	InitRenderGraph();
}

void VulkanRenderer::Init_Internal()
{
	//_firstPersonPass = std::make_unique<FirstPersonPass>("shader/FPS/firstperson.vs", "shader/FPS/firstperson.fs");

	_combinPass = std::make_unique<CombinPass>("shader/postprocess/combin.comp");
	_globalBloomPass = std::make_unique<BloomPass>("shader/postprocess/bloomblur.comp", scr_width, scr_height);
	_globalPostProcessPass = std::make_unique<GlobalPostProcessPass>("shader/postprocess/globalpostprocess.comp");

	InitRenderTarget();

	_cameraCache.curUBO = std::make_shared<UniformBlock>(sizeof(comp_camera));
	_cameraCache.prevUBO = std::make_shared<UniformBlock>(sizeof(comp_camera));
}

void VulkanRenderer::InitRenderTarget()
{
	const Texture2DConfig config
	{
		.minFilter = vk::Filter::eLinear,
		.magFilter = vk::Filter::eLinear,
		.wrapU = vk::SamplerAddressMode::eClampToEdge,
		.wrapV = vk::SamplerAddressMode::eClampToEdge,
		.anisotropy = false,
		.gammaCorrection = false
	};

	_renderTarget.sceneColorBuffer = std::make_shared<Texture2D>(scr_width, scr_height, vk::Format::eR16G16B16A16Sfloat, config);
	_renderTarget.sceneDepthBuffer = std::make_shared<Texture2D>(scr_width, scr_height, vk::Format::eD24UnormS8Uint, config);

	_renderTarget.firstPersonColorBuffer = std::make_shared<Texture2D>(scr_width, scr_height, vk::Format::eR16G16B16A16Sfloat, config);
	_renderTarget.firstPersonDepthBuffer = std::make_shared<Texture2D>(scr_width, scr_height, vk::Format::eD24UnormS8Uint, config);

	_renderTarget.combinColorBuffer = std::make_shared<Texture2D>(scr_width, scr_height, vk::Format::eR16G16B16A16Sfloat, config);
	_renderTarget.combinBrightColorBuffer = std::make_shared<Texture2D>(scr_width, scr_height, vk::Format::eR16G16B16A16Sfloat, config);

	auto cmd = VKCONTEXT->GetCommandBuffer();
	cmd->Begin();
	_renderTarget.sceneColorBuffer->TransitionLayout(cmd, nullptr, Texture2D::BindStage::Graphics, Texture2D::BindUsage::Output);
	cmd->End();
	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
}

std::shared_ptr<VKCore::VulkanInstance> VulkanRenderer::GetVulkanInstance() const {
	return _vulkanInstance;
}

std::shared_ptr<VKCore::VulkanSurface> VulkanRenderer::GetVulkanSurface() const {
	return _vulkanSurface;
}

std::shared_ptr<VKCore::VulkanDevice> VulkanRenderer::GetVulkanDevice() const {
	return _vulkanDevice;
}

std::shared_ptr<VKCore::VulkanSwapchain> VulkanRenderer::GetVulkanSwapchain() const {
	return _vulkanSwapchain;
}

//void VulkanRenderer::Draw(RenderState& state)
//{
//	SetupRenderState(state);
//	//SetupIndirectDrawData(state);
//
//	_sceneRenderGraph->Execute(state);
//	//_firstPersonRenderGraph->Execute(state);
//
//
//	//RenderFirstPersonLayer(state);
//
//	static bool draw = false;
//	if (draw)
//		DrawTexture(_renderTarget.sceneColorBuffer, "temp/color.png");
//
//	_combinPass->Draw(_renderTarget.combinFbo, { _renderTarget.sceneColorBuffer->GetID() ,_renderTarget.firstPersonColorBuffer->GetID() });
//
//	auto& option = state.option;
//	if (option.flags.bloomOn) _globalBloomPass->Draw(_renderTarget.combinBrightColorBuffer->GetID());
//	_globalPostProcessPass->Draw(
//		_renderTarget.finalFbo, _renderTarget.combinColorBuffer->GetID(), _globalBloomPass->GetBloomBlurMap(),
//		option.flags.bloomOn, option.flags.gammaOn, needFlipFinalFboY,
//		pow(2.0f, option.postProcessParams.EV100), option.postProcessParams.gamma
//	);
//	FinishRendering(state);
//}

struct MatrixUBOData {
	glm::mat4 model = glm::mat4(1.0f);
	glm::mat4 projection;
	glm::mat4 view;
};

void VulkanRenderer::Draw(RenderState& state)
{
	if (_mode == RenderMode::Present)
		DrawPresent(state);
	else if (_mode == RenderMode::SharedTexture)
		DrawSharedTexture(state);
	else if (_mode == RenderMode::Offscreen)
		DrawOffScreen(state);
}

std::shared_ptr<Texture2D> VulkanRenderer::GetColorBuffer() const
{
	return _renderTarget.finalColorBuffer;
}

int VulkanRenderer::GetWidth() const
{
	return scr_width;
}

int VulkanRenderer::GetHeight() const
{
	return scr_height;
}

RenderOption VulkanRenderer::GetOption() const
{
	return _option;
}

void VulkanRenderer::SetOption(RenderOption option)
{
	_option = option;
}

void VulkanRenderer::Resize(uint32_t width, uint32_t height)
{
	width = std::max(1u, width);
	height = std::max(1u, height);
	if (scr_width == width && scr_height == height)
		return;

	if (_mode == RenderMode::Offscreen)
	{
		scr_width = width;
		scr_height = height;
		InitForOffSceen(scr_width, scr_height);
	}
}

void VulkanRenderer::InitRenderGraph()
{
	InitSceneRenderGraph();
	InitFirstPersonRenderGraph();
}

// RenderGraphResource生成器，输入描述信息或者参照物（如其他res，已有的Texture2D），获取资源声明
class ResourceBuilder
{
public:
	ResourceBuilder(std::unique_ptr<RenderGraph::Graph>& graph) :_graph(graph) {}

public:
	RenderGraph::RenderGraphResource CreateTexture(
		uint32_t width, uint32_t height,
		vk::Format format,
		vk::Filter minFilter, vk::Filter magFilter,
		vk::SamplerAddressMode wrapU, vk::SamplerAddressMode wrapV,
		const RenderGraph::ResourceName& name,
		uint32_t maxLevel = 1) {
		return _graph->CreateTexture(RenderGraph::TextureDesc{ width ,height,format,minFilter,magFilter,wrapU,wrapV,std::max(1u,maxLevel) }, name);
	}
	RenderGraph::RenderGraphResource CreateTexture(uint32_t width, uint32_t height, vk::Format format, vk::Filter filter, vk::SamplerAddressMode wrap, const RenderGraph::ResourceName& name, uint32_t maxLevel = 1) {
		return _graph->CreateTexture(RenderGraph::TextureDesc{ width ,height, format, filter, filter, wrap, wrap, std::max(1u,maxLevel) }, name);
	}
	RenderGraph::RenderGraphResource CreateTexture(const RenderGraph::RenderGraphResource& other, const RenderGraph::ResourceName& name) {
		return _graph->CreateTexture(std::get<RenderGraph::TextureDesc>(other.desc), name);
	}
	RenderGraph::RenderGraphResource CreateTexture(const std::shared_ptr<Texture2D>& tex, const RenderGraph::ResourceName& name) {
		return CreateTexture(tex->GetWidth(), tex->GetHeight(), tex->GetFormat(), tex->GetMinFilter(), tex->GetMagFilter(), tex->GetWrapU(), tex->GetWrapV(), name, tex->GetMaxLevel());
	}
	RenderGraph::ExternalResource CreateExternalTxture(const RenderGraph::ResourceName& name) {
		return _graph->CreateExternalTexture(name);
	}

private:
	std::unique_ptr<RenderGraph::Graph>& _graph;
};

void VulkanRenderer::InitSceneRenderGraph()
{
	int width = scr_width;
	int height = scr_height;

	_sceneRenderGraph = std::make_unique<RenderGraph::Graph>("SceneRenderGraph");

	// 注入RenderTarget
	auto Ext_RenderTargetColorBuffer = _sceneRenderGraph->InjectExternalTexture("renderTargetColorBuffer", _renderTarget.sceneColorBuffer);
	auto Ext_RenderTargetDepthBuffer = _sceneRenderGraph->InjectExternalTexture("renderTargetDepthBuffer", _renderTarget.sceneDepthBuffer);

	ResourceBuilder resbuilder(_sceneRenderGraph);

	auto gPosition = resbuilder.CreateTexture(width, height, vk::Format::eR32G32B32A32Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "gPosition");
	auto gNormal = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "gNormal");
	auto gAlbedoOpacity = resbuilder.CreateTexture(width, height, vk::Format::eR8G8B8A8Unorm, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "gAlbedoOpacity");
	auto gMetallicRoughnessMap = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "gMetallicRoughnessMap");
	auto gMotionVectorMap = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "gMotionVectorMap");
	auto gEmission = resbuilder.CreateTexture(width, height, vk::Format::eR8G8B8A8Unorm, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "gEmission");
	auto gDepthStencilMap = resbuilder.CreateTexture(_renderTarget.sceneDepthBuffer, "geometryPass_TempDepthStencilMap");
	auto ssaoOutPut = resbuilder.CreateTexture(width, height, vk::Format::eR8Unorm, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "ssaoOutPutBuffer");
	auto rayTraceReflect_Output = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "rayTraceReflect_Output");
	auto rayTraceGI_Output = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "rayTraceGI_Output");
	auto ssr_Output = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "ssr_Output");
	auto ssgi_Output = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "ssgi_Output");
	auto atlasShadowMap = resbuilder.CreateTexture(1024, 1024, vk::Format::eD32Sfloat, vk::Filter::eLinear, vk::SamplerAddressMode::eClampToEdge, "atlasShadowMap");

	// 不透明物体
	auto preCalculatePass = _sceneRenderGraph->AddPass("preCalculatePass");
	auto hzbPass = _sceneRenderGraph->AddPass("hzbPass");
	auto geometryPass = _sceneRenderGraph->AddPass("geometry");
	auto lightingShadowDepthPass = _sceneRenderGraph->AddPass("lightingShadowDepth");
	auto ssaoPass = _sceneRenderGraph->AddPass("ssaoPass");
	auto lightingPass = _sceneRenderGraph->AddPass("lightingPass");
	auto copyDepthPass = _sceneRenderGraph->AddPass("copyDepthPass");
	auto skyBoxPass = _sceneRenderGraph->AddPass("skyBoxPass");
	auto rayTraceGeneralPass = _sceneRenderGraph->AddPass("rayTraceGeneralPass");
	auto rayTraceReflectPass = _sceneRenderGraph->AddPass("rayTraceReflectPass");
	auto rayTraceGIPass = _sceneRenderGraph->AddPass("rayTraceGIPass");
	auto ssrPass = _sceneRenderGraph->AddPass("ssrPass");
	auto ssgiPass = _sceneRenderGraph->AddPass("ssgiPass");
	auto combinIndirectLightingPass = _sceneRenderGraph->AddPass("combinIndirectLightingPass");
	auto lightDrawPass = _sceneRenderGraph->AddPass("lightDrawPass");
	auto opaqueFence = _sceneRenderGraph->AddFence("opaqueFence");

	// 透明物体
	auto effectPass = _sceneRenderGraph->AddPass("effectPass");
	auto transparentPass = _sceneRenderGraph->AddPass("transparentPass");
	auto transprantFence = _sceneRenderGraph->AddFence("transprantFence");
	transprantFence->After(opaqueFence);

	// 后处理
	auto depthFogPass = _sceneRenderGraph->AddPass("depthFogPass");
	auto postProcessFence = _sceneRenderGraph->AddFence("postProcessFence");
	postProcessFence->After(transprantFence);

	// 曝光计算
	auto autoExposurePass = _sceneRenderGraph->AddPass("autoExposurePass");

	preCalculatePass->SetRenderPass(std::make_unique<PreCalculatePass>());

	auto hzbRender = std::make_unique<HZBPass>(
		"shader/HZB/depth.vs",
		"shader/HZB/depth.fs",
		"shader/HZB/HZBGenerate.comp",
		"shader/HZB/occlusionCulling.comp"
	);
	auto hzbMap = resbuilder.CreateTexture(width, height, vk::Format::eD32Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "HZBMap", hzbRender->GetMaxLevel());

	hzbPass->SetRenderPass(std::move(hzbRender))
		.Temp(resbuilder.CreateTexture(width, height, vk::Format::eD32Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "hzbPass_temp"))
		.Output(hzbMap)
		.After(preCalculatePass)
		.Before(geometryPass);

	lightingShadowDepthPass->SetRenderPass(std::make_unique<LightShadowDepthPass>(
		"shader/lighting/AMDViewport_Dirlightshadow_StaticMesh.vs",
		"shader/lighting/AMDViewport_Dirlightshadow_Skinned.vs",
		"shader/lighting/AMDViewport_Dirlightshadow.fs",
		"shader/lighting/AMDViewport_Pointlightshadow_StaticMesh.vs",
		"shader/lighting/AMDViewport_Pointlightshadow_Skinned.vs",
		"shader/lighting/AMDViewport_Pointlightshadow.fs"
	))
		.Persistent(atlasShadowMap)
		.After(preCalculatePass)
		.Before(lightingPass);

	geometryPass->SetRenderPass(std::make_unique<GeometryPass>(
		"shader/gbuffer/geometrypass_StaticMesh.vs",
		"shader/gbuffer/geometrypass_StaticMesh.fs",
		"shader/gbuffer/geometrypass_SkinnedMesh.vs",
		"shader/gbuffer/geometrypass_SkinnedMesh.fs"))
		.Output(gPosition, gNormal, gAlbedoOpacity, gMetallicRoughnessMap, gMotionVectorMap, gDepthStencilMap, gEmission)
		.After(hzbPass, preCalculatePass)
		.Before(lightingPass);

	ssaoPass->SetRenderPass(std::make_unique<SSAOPass>("shader/ssao/ssao.comp", "shader/ssao/ssaoblur.comp"));
	ssaoPass->Input(gPosition, gNormal)
		.Temp(resbuilder.CreateTexture(ssaoOutPut, "ssaoColorBuffer"))
		.Output(ssaoOutPut)
		.After(geometryPass)
		.Before(lightingPass);

	lightingPass->SetRenderPass(std::make_unique<LightingPass>("shader/lighting/lightingpass.comp"))
		.Input(gPosition, gNormal, gAlbedoOpacity, gMetallicRoughnessMap, atlasShadowMap, ssaoOutPut, gEmission)
		.External(Ext_RenderTargetColorBuffer)
		.After(lightingShadowDepthPass, ssaoPass)
		.Before(opaqueFence);

	copyDepthPass->SetRenderPass(MakeLambdaPass([](const RenderGraph::PassContext& ctx, RenderState& state)-> void
		{
			auto geometryDepthStencil = ctx.GetInput(0);
			auto renderTargetDepthBuffer = ctx.GetExternal(0);
			Texture2D::CopyTexture(geometryDepthStencil, renderTargetDepthBuffer);
		}))
		.Input(gDepthStencilMap)
		.External(Ext_RenderTargetDepthBuffer)
		.Before(skyBoxPass, opaqueFence);

	skyBoxPass->SetRenderPass(std::make_unique<SkyBoxPass>("shader/skybox/skybox.comp"))
		.External(Ext_RenderTargetColorBuffer, Ext_RenderTargetDepthBuffer)
		.After(copyDepthPass)
		.Before(opaqueFence);

	auto generalPass = std::make_unique<RayTraceGeneralPass>();
	auto reflectPass = std::make_unique<RayTraceReflectPass>("shader/RayTrace/RayTraceReflect.comp", "shader/RayTrace/BilateralFilterBlur.comp", "shader/RayTrace/TemporalAccumulate.comp", "shader/General/imagescale.comp");
	auto giPass = std::make_unique<RayTraceGIPass>("shader/RayTrace/RayTraceGI.comp", "shader/RayTrace/BilateralFilterBlur.comp", "shader/RayTrace/TemporalAccumulate.comp", "shader/General/imagescale.comp");
	giPass->SetGeneralBuffer(generalPass->GetGeneralBuffer());
	reflectPass->SetGeneralBuffer(generalPass->GetGeneralBuffer());

	rayTraceGeneralPass->SetRenderPass(std::move(generalPass));

	rayTraceReflectPass->SetRenderPass(std::move(reflectPass))
		.Input(gPosition, gNormal, gAlbedoOpacity, gMetallicRoughnessMap, atlasShadowMap, ssaoOutPut, gMotionVectorMap)
		.Temp(resbuilder.CreateTexture(rayTraceReflect_Output, "rayTraceReflect_Temp1"), resbuilder.CreateTexture(rayTraceReflect_Output, "rayTraceReflect_Temp2"))
		.External(Ext_RenderTargetDepthBuffer)
		.Output(rayTraceReflect_Output)
		.Persistent(resbuilder.CreateTexture(rayTraceReflect_Output, "rayTraceReflect_historyColorTexture"))
		.After(copyDepthPass, rayTraceGeneralPass)
		.Before(opaqueFence);

	rayTraceGIPass->SetRenderPass(std::move(giPass))
		.Input(gPosition, gNormal, gAlbedoOpacity, gMetallicRoughnessMap, atlasShadowMap, ssaoOutPut, gMotionVectorMap)
		.Temp(resbuilder.CreateTexture(rayTraceGI_Output, "rayTraceGI_Temp1"), resbuilder.CreateTexture(rayTraceGI_Output, "rayTraceGI_Temp2"))
		.External(Ext_RenderTargetDepthBuffer)
		.Output(rayTraceGI_Output)
		.Persistent(resbuilder.CreateTexture(rayTraceGI_Output, "rayTraceGI_historyColorTexture"))
		.After(copyDepthPass, rayTraceGeneralPass)
		.Before(opaqueFence);

	ssrPass->SetRenderPass(std::make_unique<SSRPass>("shader/ssr/SSReflect.comp", "shader/ssr/BilateralFilterBlur.comp", "shader/ssr/TemporalAccumulate.comp"))
		.Input(gPosition, gNormal, gAlbedoOpacity, gMetallicRoughnessMap, gMotionVectorMap, hzbMap)
		.Temp(resbuilder.CreateTexture(ssr_Output, "ssrPass_temp1"), resbuilder.CreateTexture(ssr_Output, "ssrPass_temp2"))
		.External(Ext_RenderTargetColorBuffer, Ext_RenderTargetDepthBuffer)
		.Output(ssr_Output)
		.Persistent(resbuilder.CreateTexture(ssr_Output, "ssrPass_historyColorTexture"))
		.After(copyDepthPass, lightingPass)
		.Before(opaqueFence);

	ssgiPass->SetRenderPass(std::make_unique<SSGIPass>("shader/ssr/SSGI.comp", "shader/ssr/BilateralFilterBlur.comp", "shader/ssr/TemporalAccumulate.comp"))
		.Input(gPosition, gNormal, gAlbedoOpacity, gMetallicRoughnessMap, ssaoOutPut, gMotionVectorMap, hzbMap)
		.Temp(resbuilder.CreateTexture(ssgi_Output, "ssgiPass_temp1"), resbuilder.CreateTexture(ssgi_Output, "ssgiPass_temp2"))
		.External(Ext_RenderTargetColorBuffer, Ext_RenderTargetDepthBuffer)
		.Output(ssgi_Output)
		.Persistent(resbuilder.CreateTexture(ssgi_Output, "ssgiPass_historyColorTexture"))
		.After(copyDepthPass, lightingPass)
		.Before(opaqueFence);

	constexpr uint32_t work_size_x = 16;
	constexpr uint32_t work_size_y = 16;
	auto makeCombinShader = [&](const std::string& path) -> std::shared_ptr<ComputePipeline>
		{
			auto shader = std::make_shared<ComputePipeline>();

			ComputePipelineConfig config;
			config.AddDefineMacro("COMBIN_MODE", 1);
			config.AddDefineMacro("SkipBrightOutput", "");
			config.AddDefineMacro("work_size_x", work_size_x);
			config.AddDefineMacro("work_size_y", work_size_y);
			config.computePath = "shader/postprocess/combin.comp";

			config
				.AddStorageImage(0)
				.AddStorageImage(1)
				.AddUnifromVariableTextureArray(2, 20)
				.AddPushConstant(sizeof(uint32_t));

			if (config.Validate())
				shader->Create(config);

			return shader;
		};

	combinIndirectLightingPass->SetRenderPass(MakeLambdaPass(
		[_shader = makeCombinShader("shader/postprocess/combin.comp"), work_size_x = work_size_x, work_size_y = work_size_y]
		(const RenderGraph::PassContext& ctx, RenderState& state) mutable -> void
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

			uint32_t width = sceneColorBuffer->GetWidth();
			uint32_t height = sceneColorBuffer->GetHeight();

			auto cmd = VKCONTEXT->GetCommandBuffer();

			_shader->SetStorageImage(sceneColorBuffer, 0);
			_shader->SetUniformTextureArray(all_tex, 2);

			uint32_t count = (uint32_t)all_tex.size();

			_shader->Bind(cmd);
			_shader->SetPushConstants(cmd, &count, sizeof(count));

			cmd->dispatch((width + work_size_x - 1) / work_size_x, (height + work_size_y - 1) / work_size_y, 1);

			VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
		}))
		.InputOption(rayTraceReflect_Output, rayTraceGI_Output, ssr_Output, ssgi_Output)
		.External(Ext_RenderTargetColorBuffer)
		.Temp(resbuilder.CreateTexture(_renderTarget.sceneColorBuffer, "combinIndirectLightingPass_temp1"))
		.After(rayTraceReflectPass, ssrPass, ssgiPass)
		.Before(opaqueFence);

	lightDrawPass->SetRenderPass(std::make_unique<LightDrawPass>("shader/lighting/lightMesh.vs", "shader/lighting/lightMesh.fs"))
		.External(Ext_RenderTargetColorBuffer, Ext_RenderTargetDepthBuffer)
		.After(combinIndirectLightingPass)
		.Before(opaqueFence);

	//effectPass->SetRenderPass(std::make_unique<EffectPass>("shader/effect/effectpass.vs", "shader/effect/effectpass.fs"))
	//	.After(opaqueFence)
	//	.Before(transprantFence);

	//transparentPass->SetRenderPass(std::make_unique<TransparentPass>("shader/Transparent/transparentpass.vs", "shader/Transparent/transparentpass.fs"))
	//	.Input(atlasShadowMap)
	//	.After(opaqueFence, effectPass)
	//	.Before(transprantFence);

	depthFogPass->SetRenderPass(std::make_unique<DepthFogPass>("shader/postprocess/depthFog.comp"))
		.After(opaqueFence, transprantFence)
		.External(Ext_RenderTargetColorBuffer, Ext_RenderTargetDepthBuffer)
		.Temp(resbuilder.CreateTexture(_renderTarget.sceneColorBuffer, "depthFogPass_TempColor"))
		.Before(postProcessFence);

	autoExposurePass->SetRenderPass(std::make_unique<AutoExposurePass>("shader/AutoExposure/histogram.comp"))
		.After(postProcessFence)
		.External(Ext_RenderTargetColorBuffer);

	_sceneRenderGraph->Compile();
}

void VulkanRenderer::InitFirstPersonRenderGraph()
{
	uint32_t width = scr_width;
	uint32_t height = scr_height;

	_firstPersonRenderGraph = std::make_unique<RenderGraph::Graph>("FirstPersonRenderGraph");

}

void VulkanRenderer::Draw_Internal(RenderState& state)
{
	SetupRenderState(state);
	SetupIndirectDrawData(state);

	_sceneRenderGraph->Execute(state);
	_firstPersonRenderGraph->Execute(state);
	//static bool draw = false;
	//if (draw)
	//	DrawTexture(_renderTarget.sceneColorBuffer, "temp/color.png");

	_combinPass->Draw(_renderTarget.combinColorBuffer, _renderTarget.combinBrightColorBuffer, { _renderTarget.sceneColorBuffer ,_renderTarget.firstPersonColorBuffer });

	auto& option = state.option;
	if (option.flags.bloomOn) _globalBloomPass->Draw(_renderTarget.combinBrightColorBuffer);
	_globalPostProcessPass->Draw(
		_renderTarget.finalColorBuffer, _renderTarget.combinColorBuffer, _globalBloomPass->GetBloomBlurMap(),
		option.flags.bloomOn, option.flags.gammaOn, needFlipFinalY,
		pow(2.0f, option.postProcessParams.EV100), option.postProcessParams.gamma
	);

	FinishRendering(state);
}

void VulkanRenderer::DrawPresent(RenderState& state)
{
	uint32_t semaphoreIndex = state.renderRecord.frameIndex % _vulkanSwapchain->GetSwapchainImageCount();
	auto& imageAcquiredSemaphore = _imageAcquiredSemaphores[semaphoreIndex];
	auto& renderFinishedSemaphore = _renderFinishedSemaphores[semaphoreIndex];

	//获取交换链图像索引
	_vulkanSwapchain->SwapImage(*imageAcquiredSemaphore);
	auto imageIndex = _vulkanSwapchain->GetCurrentImageIndex();

	static MatrixUBOData matrixData;
	matrixData.view = state.camera.view;
	matrixData.projection = state.camera.projection;
	auto matrixBlock = std::make_shared<UniformBlock>(sizeof(MatrixUBOData));
	matrixBlock->WriteData(&matrixData, sizeof(MatrixUBOData), 0);

	auto cmd = VKCONTEXT->GetCommandBuffer();

	auto colorAttachmentImage = _vulkanSwapchain->SwapchainImage()[imageIndex];

	auto colorAttachmentImageView = _vulkanSwapchain->SwapchainImageView()[imageIndex];
	auto depthStencilAttachmentImageView = _depthImages[imageIndex]->GetImageView();

	{
		cmd->Begin();
		vk::ImageMemoryBarrier barrier;
		barrier
			.setOldLayout(vk::ImageLayout::eUndefined)
			.setNewLayout(vk::ImageLayout::eTransferDstOptimal)
			.setImage(colorAttachmentImage)
			.setSubresourceRange(vk::ImageSubresourceRange()
				.setAspectMask(vk::ImageAspectFlagBits::eColor)
				.setBaseMipLevel(0)
				.setLevelCount(1)
				.setBaseArrayLayer(0)
				.setLayerCount(1));

		cmd->pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe,
			vk::PipelineStageFlagBits::eColorAttachmentOutput,
			vk::DependencyFlagBits::eByRegion,
			{}, {}, barrier
		);
		cmd->End();
		CmdSyncSeamphore data{ .waitSemaphores = {{imageAcquiredSemaphore}} };
		VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd, data);
	}

	{
		Draw_Internal(state);
	}

	{
		cmd->Begin();

		_renderTarget.finalColorBuffer->TransitionLayout(cmd, nullptr);

		Texture2D::BlitImage(cmd, *_renderTarget.finalColorBuffer, _vulkanSwapchain->SwapchainImage()[imageIndex], scr_width, scr_height);

		vk::ImageMemoryBarrier presentBarrier;
		presentBarrier
			.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
			.setNewLayout(vk::ImageLayout::ePresentSrcKHR)
			.setImage(_vulkanSwapchain->SwapchainImage()[imageIndex])
			.setSubresourceRange(vk::ImageSubresourceRange()
				.setAspectMask(vk::ImageAspectFlagBits::eColor)
				.setBaseMipLevel(0)
				.setLevelCount(1)
				.setBaseArrayLayer(0)
				.setLayerCount(1));

		cmd->pipelineBarrier(
			vk::PipelineStageFlagBits::eColorAttachmentOutput,
			vk::PipelineStageFlagBits::eBottomOfPipe,
			vk::DependencyFlagBits::eByRegion,
			{}, {}, presentBarrier
		);

		cmd->End();
		CmdSyncSeamphore data{ .signalSemaphores = {renderFinishedSemaphore} };
		VKCONTEXT->SubmitCommandImmediately(cmd, data, _fence);
		_vulkanSwapchain->PresentImage(*renderFinishedSemaphore);
		_fence->WaitAndReset();
	}
}

void VulkanRenderer::DrawSharedTexture(RenderState& state)
{
	Draw_Internal(state);

	auto cmd = VKCONTEXT->GetCommandBuffer();
	_renderTarget.finalColorBuffer->TransitionLayout(cmd, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eBottomOfPipe);

	//vk::ClearColorValue clearColor = { 1.0f, 1.0f, 0.0f, 1.0f };
	//vk::ImageSubresourceRange range;
	//range.setAspectMask(vk::ImageAspectFlagBits::eColor)
	//	.setBaseArrayLayer(0)
	//	.setLayerCount(1)
	//	.setBaseMipLevel(0)
	//	.setLevelCount(1);
	//cmd->clearColorImage(_renderTarget.finalColorBuffer->GetImage(), vk::ImageLayout::eGeneral, clearColor, range);

	if (cmd->IsRecording())
		VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

}

void VulkanRenderer::DrawOffScreen(RenderState& state)
{
	Draw_Internal(state);
}

void VulkanRenderer::EarlyProcess(RenderState& state)
{
	state.renderRecord.frameIndex = _record.frameIndex++;
	state.option = _option;

	_sceneRenderGraph->EarlyExecute(state);
	_firstPersonRenderGraph->EarlyExecute(state);
}

void VulkanRenderer::SetupRenderState(RenderState& state)
{
	//glBindFramebuffer(GL_FRAMEBUFFER, _renderTarget.sceneFbo);
	//glViewport(0, 0, scr_width, scr_height);
	//glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	_cameraCache.prevUBO->WriteData(&_cameraCache.data, sizeof(comp_camera));

	_cameraCache.data.projection = state.camera.projection;
	_cameraCache.data.view = state.camera.view;
	_cameraCache.data.projView = _cameraCache.data.projection * _cameraCache.data.view;
	_cameraCache.data.invProjection = glm::inverse(state.camera.projection);
	_cameraCache.data.invView = glm::inverse(state.camera.view);
	_cameraCache.data.invProjView = glm::inverse(_cameraCache.data.projView);
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
	_cameraCache.curUBO->WriteData(&_cameraCache.data, sizeof(comp_camera));

	state.camera.prevUBO = _cameraCache.prevUBO;
	state.camera.curUBO = _cameraCache.curUBO;

	state.framebuffer.width = scr_width;
	state.framebuffer.height = scr_height;

	state.renderRecord.prevEV100 = _record.prevEV100;
	state.renderRecord.prevRenderMicroTimeStamp = _record.prevRenderMicroTimeStamp;
	state.renderRecord.currentRenderMicroTimeStamp = Tool::GetTimestampMircoseconds();

}

void VulkanRenderer::SetupIndirectDrawData(RenderState& state)
{

	auto bindlessTextureManager = BindlessTextureManager::Instance();
	auto indirectManager = IndirectDrawManager::Instance();

	{
		auto& items = state.objects.sceneRenderData.opaqueMesh;
		for (auto& item : items)
		{
			auto& material = item.meshinfo.material;
			for (auto& [type, tex] : material->GetTextures())
				bindlessTextureManager->RegisterOrUpdateTexture(tex);
			indirectManager->setupMaterial(*material);

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
			auto& material = item.meshinfo.material;
			for (auto& [type, tex] : material->GetTextures())
				bindlessTextureManager->RegisterOrUpdateTexture(tex);
			indirectManager->setupMaterial(*material);

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
				auto& material = info.material;
				for (auto& [type, tex] : material->GetTextures())
					bindlessTextureManager->RegisterOrUpdateTexture(tex);
				indirectManager->setupMaterial(*material);

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
			auto& material = item.meshinfo.material;
			for (auto& [type, tex] : material->GetTextures())
				bindlessTextureManager->RegisterOrUpdateTexture(tex);
			indirectManager->setupMaterial(*material);

			auto& mesh = item.meshinfo.mesh;
			if (mesh->GetNeedUpdateIndricetDraw())
			{
				indirectManager->setupMesh(*mesh);
				mesh->SetNeedUpdateIndirectDraw(false);
			}
		}
	}
}

void VulkanRenderer::FinishRendering(RenderState& state)
{
	_record.prevEV100 = state.option.postProcessParams.EV100;
	_record.prevRenderMicroTimeStamp = state.renderRecord.currentRenderMicroTimeStamp;
}
//
//void VulkanRenderer::RenderFirstPersonLayer(RenderState& state)
//{
//	_firstPersonPass->Draw(state);
//}

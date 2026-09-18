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

#include "VulkanRenderEngine/RenderPass/RTCoreRayTraceGeneralPass.h"
#include "VulkanRenderEngine/RenderPass/RTCoreRayTraceGIPass.h"
#include "VulkanRenderEngine/RenderPass/RTCoreRayTraceReflectPass.h"

const std::string Ext_RenderTargetColorBuffer_Name = "renderTargetColorBuffer";
const std::string Ext_RenderTargetDepthBuffer_Name = "renderTargetDepthBuffer";

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

std::shared_ptr<VKCore::VulkanDevice> CreateVKDevice(std::shared_ptr<VKCore::VulkanInstance> instance, VKCore::VulkanPhysicalDeviceInfo info)
{
	auto vulkanDevice = std::make_shared<VKCore::VulkanDevice>();
	vulkanDevice->AddDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	vulkanDevice->AddDeviceExtension(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
	vulkanDevice->AddDeviceExtension(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
	vulkanDevice->AddDeviceExtension(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
	vulkanDevice->AddDeviceExtension(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);
	vulkanDevice->AddDeviceExtension(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
	//vulkanDevice->AddDeviceExtension(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);

	if (GlobalConfig::RTCoreEnable)
	{
		vulkanDevice->AddDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
		vulkanDevice->AddDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
		vulkanDevice->AddDeviceExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
		vulkanDevice->AddDeviceExtension(VK_KHR_RAY_TRACING_POSITION_FETCH_EXTENSION_NAME);
		vulkanDevice->AddDeviceExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME);
		vulkanDevice->AddDeviceExtension(VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME);
		//vulkanDevice->AddDeviceExtension(VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME);
		//vulkanDevice->AddDeviceExtension(VK_EXT_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME);
	}

	if (vulkanDevice->Create(instance, info) != vk::Result::eSuccess)
		return nullptr;

	return vulkanDevice;
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

	auto vulkanDevice = CreateVKDevice(instance, info);
	if (!vulkanDevice)
		return nullptr;

	VKCONTEXT->SetDevice(vulkanDevice);

	auto vulkanSwapchain = std::make_shared<VKCore::VulkanSwapchain>();

	// 除了MaxFramesInFlight之外，交换链需要预留一张图处于呈现状态
	// 故目标图像数量为MaxFramesInFlight + 1
	if (auto result = vulkanSwapchain->Create(vulkanDevice, vulkanSurface, { width, height }, GlobalConfig::MaxFramesInFlight + 1, limitFrameRate); result != vk::Result::eSuccess)
		return nullptr;

	auto renderer = std::make_shared<VulkanRenderer>();
	renderer->_vulkanInstance = instance;
	renderer->_vulkanSurface = vulkanSurface;
	renderer->_vulkanDevice = vulkanDevice;
	renderer->_vulkanSwapchain = vulkanSwapchain;
	renderer->_maxFramesInFlight = std::max(1u, std::min(GlobalConfig::MaxFramesInFlight, vulkanSwapchain->GetSwapchainImageCount() - 1));
	renderer->InitForWindow(width, height);

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

	auto vulkanDevice = CreateVKDevice(instance, info);
	if (!vulkanDevice)
		return nullptr;

	VKCONTEXT->SetDevice(vulkanDevice);

	auto renderer = std::make_shared<VulkanRenderer>();
	renderer->_vulkanInstance = instance;
	renderer->_vulkanDevice = vulkanDevice;
	renderer->_maxFramesInFlight = std::max(1u, GlobalConfig::MaxFramesInFlight);
	renderer->InitForOffSceen(width, height);

	return renderer;
}

std::shared_ptr<VulkanRenderer> VulkanRenderer::CreateOnlyDevice(std::shared_ptr<VKCore::VulkanInstance> instance, VkSurfaceKHR surface)
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

	auto vulkanDevice = CreateVKDevice(instance, info);
	if (!vulkanDevice)
		return nullptr;

	VKCONTEXT->SetDevice(vulkanDevice);

	auto renderer = std::make_shared<VulkanRenderer>();
	renderer->_vulkanInstance = instance;
	renderer->_vulkanSurface = vulkanSurface;
	renderer->_vulkanDevice = vulkanDevice;

	return renderer;
}

std::shared_ptr<VulkanRenderer> VulkanRenderer::CreateOnlyDevice(std::shared_ptr<VKCore::VulkanInstance> instance)
{
	if (!instance) return nullptr;

	VKCONTEXT->SetInstance(instance);

	VKCore::VulkanPhysicalDeviceInfo info;
	if (!instance->GetSuitablePhysicalDevice(info, true, true))
	{
		std::cout << std::format("[ CreateForOffscreen ] ERROR\nFailed to get suitable physical device\n");
		return nullptr;
	}

	auto vulkanDevice = CreateVKDevice(instance, info);
	if (!vulkanDevice)
		return nullptr;

	VKCONTEXT->SetDevice(vulkanDevice);

	auto renderer = std::make_shared<VulkanRenderer>();
	renderer->_vulkanInstance = instance;
	renderer->_vulkanDevice = vulkanDevice;

	return renderer;
}

bool VulkanRenderer::CreateForWindow_Target(std::shared_ptr<VulkanRenderer>& renderer, uint32_t width, uint32_t height, bool limitFrameRate)
{
	auto vulkanSwapchain = std::make_shared<VKCore::VulkanSwapchain>();

	if (auto result = vulkanSwapchain->Create(renderer->_vulkanDevice, renderer->_vulkanSurface, { width, height }, std::max(2u, GlobalConfig::MaxFramesInFlight + 1), limitFrameRate); result != vk::Result::eSuccess)
		return false;

	renderer->_vulkanSwapchain = vulkanSwapchain;
	renderer->_maxFramesInFlight = std::max(1u, std::min(GlobalConfig::MaxFramesInFlight, vulkanSwapchain->GetSwapchainImageCount() - 1));
	renderer->InitForWindow(width, height);
	return true;
}

bool VulkanRenderer::CreateForOffScreen_Target(std::shared_ptr<VulkanRenderer>& renderer, uint32_t width, uint32_t height)
{
	renderer->_maxFramesInFlight = std::max(1u, GlobalConfig::MaxFramesInFlight);
	renderer->InitForOffSceen(width, height);
	return true;
}

VulkanRenderer::VulkanRenderer()
	:_frameTaskPool(std::max(std::max(1u, GlobalConfig::MaxFramesInFlight), std::thread::hardware_concurrency()))
{
	scr_width = 0;
	scr_height = 0;
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

	_imageAcquiredSemaphores.clear();
	_renderFinishedSemaphores.clear();
	for (int i = 0; i < _vulkanSwapchain->GetSwapchainImageCount(); i++)
	{
		_imageAcquiredSemaphores.push_back(std::move(std::make_shared<VKWrapper::VKBinarySemaphore>(_vulkanDevice.get())));
		_renderFinishedSemaphores.push_back(std::move(std::make_shared<VKWrapper::VKBinarySemaphore>(_vulkanDevice.get())));
	}

	InitRenderGraph();
}

void VulkanRenderer::InitForOffSceen(uint32_t width, uint32_t height)
{
	scr_width = width;
	scr_height = height;

	_mode = RenderMode::Offscreen;

	Init_Internal();

	InitRenderGraph();
}

void VulkanRenderer::Init_Internal()
{
	//_firstPersonPass = std::make_unique<FirstPersonPass>("shader/FPS/firstperson.vs", "shader/FPS/firstperson.fs");

	_combinPass = std::make_unique<CombinPass>("shader/postprocess/combin.comp");
	_globalBloomPass = std::make_unique<BloomPass>("shader/postprocess/bloomblur.comp", scr_width, scr_height);
	_globalPostProcessPass = std::make_unique<GlobalPostProcessPass>("shader/postprocess/globalpostprocess.comp");

	InitRenderTarget();
}

void VulkanRenderer::InitRenderTarget()
{
	_renderTargets.clear();

	const Texture2DConfig config
	{
		.minFilter = vk::Filter::eLinear,
		.magFilter = vk::Filter::eLinear,
		.wrapU = vk::SamplerAddressMode::eClampToEdge,
		.wrapV = vk::SamplerAddressMode::eClampToEdge,
		.anisotropy = false,
		.gammaCorrection = false
	};

	auto cmd = VKCONTEXT->GetCommandBuffer();
	for (uint32_t i = 0; i < _maxFramesInFlight * 2; i++)
	{
		auto renderTarget = std::make_shared<RenderTargetData>();

		renderTarget->sceneColorBuffer = std::make_shared<Texture2D>(scr_width, scr_height, vk::Format::eR16G16B16A16Sfloat, config);
		renderTarget->sceneDepthBuffer = std::make_shared<Texture2D>(scr_width, scr_height, vk::Format::eD24UnormS8Uint, config);

		renderTarget->firstPersonColorBuffer = std::make_shared<Texture2D>(scr_width, scr_height, vk::Format::eR16G16B16A16Sfloat, config);
		renderTarget->firstPersonDepthBuffer = std::make_shared<Texture2D>(scr_width, scr_height, vk::Format::eD24UnormS8Uint, config);

		renderTarget->combinColorBuffer = std::make_shared<Texture2D>(scr_width, scr_height, vk::Format::eR16G16B16A16Sfloat, config);
		renderTarget->combinBrightColorBuffer = std::make_shared<Texture2D>(scr_width, scr_height, vk::Format::eR16G16B16A16Sfloat, config);

		renderTarget->finalColorBuffer = std::make_shared<Texture2D>(scr_width, scr_height, vk::Format::eR8G8B8A8Unorm, config);

		renderTarget->sceneColorBuffer->TransitionLayout(cmd, nullptr, Texture2D::BindStage::Graphics, Texture2D::BindUsage::Output);

		_renderTargets.push_back(std::move(renderTarget));
	}

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

uint32_t VulkanRenderer::GetWidth() const
{
	return scr_width;
}

uint32_t VulkanRenderer::GetHeight() const
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

	bool needRestartup = !_stop;
	if (needRestartup)
		Stop();

	while (!_runningFrames.empty())
		_runningFrames.pop();
	while (!_doneFrames.empty())
		_doneFrames.pop();

	if (_mode == RenderMode::Offscreen)
	{
		scr_width = width;
		scr_height = height;
		if (_mode == RenderMode::Offscreen)
			InitForOffSceen(scr_width, scr_height);
		else if (_mode == RenderMode::Present)
			InitForWindow(scr_width, scr_height);
	}

	if (needRestartup)
		Run();
}

void VulkanRenderer::InitRenderGraph()
{
	InitSceneRenderGraph();
	//InitCombinRenderGraph();
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
	RenderGraph::RenderGraphResource CreateVariableTexture(vk::Format format, vk::Filter filter, vk::SamplerAddressMode wrap, const RenderGraph::ResourceName& name, uint32_t maxLevel = 1) {
		return _graph->CreateTexture(RenderGraph::TextureDesc{ 1 ,1, format, filter, filter, wrap, wrap, std::max(1u,maxLevel) , true }, name);
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

	auto& sceneColorBuffer = _renderTargets[0]->sceneColorBuffer;
	auto& sceneDepthBuffer = _renderTargets[0]->sceneDepthBuffer;

	auto Ext_RenderTargetColorBuffer = _sceneRenderGraph->CreateExternalTexture(Ext_RenderTargetColorBuffer_Name);
	auto Ext_RenderTargetDepthBuffer = _sceneRenderGraph->CreateExternalTexture(Ext_RenderTargetDepthBuffer_Name);

	ResourceBuilder resbuilder(_sceneRenderGraph);

	auto gPosition = resbuilder.CreateTexture(width, height, vk::Format::eR32G32B32A32Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "gPosition");
	auto gNormal = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "gNormal");
	auto gAlbedoOpacity = resbuilder.CreateTexture(width, height, vk::Format::eR8G8B8A8Unorm, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "gAlbedoOpacity");
	auto gMetallicRoughnessMap = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "gMetallicRoughnessMap");
	auto gMotionVectorMap = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "gMotionVectorMap");
	auto gEmission = resbuilder.CreateTexture(width, height, vk::Format::eR8G8B8A8Unorm, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "gEmission");
	auto gDepthStencilMap = resbuilder.CreateTexture(sceneDepthBuffer, "geometryPass_TempDepthStencilMap");
	auto ssaoOutPut = resbuilder.CreateTexture(width, height, vk::Format::eR8Unorm, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "ssaoOutPutBuffer");
	auto rayTraceReflect_Output = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "rayTraceReflect_Output");
	auto rayTraceGI_Output = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "rayTraceGI_Output");
	auto ssr_Output = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "ssr_Output");
	auto ssgi_Output = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "ssgi_Output");
	auto atlasShadowMap = resbuilder.CreateVariableTexture(vk::Format::eD32Sfloat, vk::Filter::eLinear, vk::SamplerAddressMode::eClampToEdge, "atlasShadowMap");

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
		.Output(atlasShadowMap)
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
		.Input(gPosition, gNormal, gAlbedoOpacity, gMetallicRoughnessMap, atlasShadowMap, ssaoOutPut, gEmission, gDepthStencilMap)
		.External(Ext_RenderTargetColorBuffer)
		.After(lightingShadowDepthPass, ssaoPass)
		.Before(opaqueFence);

	copyDepthPass->SetRenderPass(MakeLambdaPass([](const RenderGraph::PassFrameContext& ctx, RenderState& state)-> void
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

	if (!GlobalConfig::RTCoreEnable)
	{
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
	}
	else
	{
		auto generalPass = std::make_unique<RTCoreRayTraceGeneralPass>();
		auto reflectPass = std::make_unique<RTCoreRayTraceReflectPass>(
			"shader/RTCoreRayTrace/RayTraceReflect.rgen", "shader/RTCoreRayTrace/Miss.rmiss", "shader/RTCoreRayTrace/ClosestHit.rchit",
			"", "", "",
			"shader/RayTrace/BilateralFilterBlur.comp", "shader/RayTrace/TemporalAccumulate.comp", "shader/General/imagescale.comp");
		auto giPass = std::make_unique<RTCoreRayTraceGIPass>(
			"shader/RTCoreRayTrace/RayTraceGI.rgen", "shader/RTCoreRayTrace/Miss.rmiss", "shader/RTCoreRayTrace/ClosestHit.rchit",
			"", "", "",
			"shader/RayTrace/BilateralFilterBlur.comp", "shader/RayTrace/TemporalAccumulate.comp", "shader/General/imagescale.comp");

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
	}

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

	{

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
			(const RenderGraph::PassFrameContext& ctx, RenderState& state) mutable -> void
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

				auto cmd = VKCONTEXT->GetCommandBuffer();

				if (!Texture2D::CopyTextureAsync(cmd, sceneColorBuffer, tempColorBuffer))
					return;

				all_tex.push_back(tempColorBuffer);

				uint32_t width = sceneColorBuffer->GetWidth();
				uint32_t height = sceneColorBuffer->GetHeight();

				ComputeBindingRecord binding;

				binding.SetStorageImage(sceneColorBuffer, 0);
				binding.SetUniformTextureArray(all_tex, 2);

				uint32_t count = (uint32_t)all_tex.size();

				_shader->Bind(cmd, binding);
				_shader->SetPushConstants(cmd, &count, sizeof(count));

				cmd->dispatch((width + work_size_x - 1) / work_size_x, (height + work_size_y - 1) / work_size_y, 1);

				VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
			}))
			.InputOption(rayTraceReflect_Output, rayTraceGI_Output, ssr_Output, ssgi_Output)
			.External(Ext_RenderTargetColorBuffer)
			.Temp(resbuilder.CreateTexture(sceneColorBuffer, "combinIndirectLightingPass_temp1"))
			.After(rayTraceReflectPass, ssrPass, ssgiPass)
			.Before(opaqueFence);
	}

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
		.Temp(resbuilder.CreateTexture(sceneColorBuffer, "depthFogPass_TempColor"))
		.Before(postProcessFence);

	autoExposurePass->SetRenderPass(std::make_unique<AutoExposurePass>("shader/AutoExposure/histogram.comp"))
		.After(postProcessFence)
		.External(Ext_RenderTargetColorBuffer);

	_sceneRenderGraph->Compile();
}

void VulkanRenderer::DrawOffScreen(std::shared_ptr<FrameData> data)
{
	if (!data)
		return;

	auto& state = data->state;
	auto& sync = data->sync;

	if (!state || !sync)
		return;

	{
		auto guard = sync->MakeProgressGuard();
		VKCONTEXT->ProcessRetireAndPushFrameIndex();
	}

	{
		auto guard = sync->MakeProgressGuard();
		SetupRenderState(state);
	}

	{
		auto guard = sync->MakeProgressGuard();
		SetupIndirectDrawData(state);
	}

	{
		std::unordered_map<std::string, std::shared_ptr<Texture2D>> externalResources = {
			{ Ext_RenderTargetColorBuffer_Name, data->renderTarget->sceneColorBuffer },
			{ Ext_RenderTargetDepthBuffer_Name, data->renderTarget->sceneDepthBuffer }
		};
		auto guard = sync->MakeProgressGuard();
		_sceneRenderGraph->Execute(state, sync, externalResources);
	}

	auto& renderTarget = data->renderTarget;
	_combinPass->Draw(renderTarget->combinColorBuffer, renderTarget->combinBrightColorBuffer, { renderTarget->sceneColorBuffer ,renderTarget->firstPersonColorBuffer });

	{
		auto guard = sync->MakeProgressGuard();
		auto& option = state->option;
		if (option.flags.bloomOn) _globalBloomPass->Draw(renderTarget->combinBrightColorBuffer);
		_globalPostProcessPass->Draw(
			renderTarget->finalColorBuffer, renderTarget->combinColorBuffer, _globalBloomPass->GetBloomBlurMap(),
			option.flags.bloomOn, option.flags.gammaOn, needFlipFinalY,
			pow(2.0f, option.postProcessParams.EV100), option.postProcessParams.gamma
		);
		FinishRendering(state);
	}
}

void VulkanRenderer::PresentImage(std::shared_ptr<FrameData>& data)
{
	auto& state = data->state;
	auto& renderTarget = data->renderTarget;

	uint32_t semaphoreIndex = state->renderRecord.frameIndex % _vulkanSwapchain->GetSwapchainImageCount();
	auto& imageAcquiredSemaphore = _imageAcquiredSemaphores[semaphoreIndex];
	auto& renderFinishedSemaphore = _renderFinishedSemaphores[semaphoreIndex];

	//获取交换链图像索引
	uint32_t imageIndex = 0;
	_vulkanSwapchain->SwapImage(*imageAcquiredSemaphore, imageIndex);
	auto swapchainImage = _vulkanSwapchain->SwapchainImage()[imageIndex];

	auto cmd = VKCONTEXT->GetCommandBuffer();

	vk::ImageMemoryBarrier barrier;
	barrier
		.setOldLayout(vk::ImageLayout::eUndefined)
		.setNewLayout(vk::ImageLayout::eTransferDstOptimal)
		.setImage(swapchainImage)
		.setSubresourceRange(vk::ImageSubresourceRange()
			.setAspectMask(vk::ImageAspectFlagBits::eColor)
			.setBaseMipLevel(0)
			.setLevelCount(1)
			.setBaseArrayLayer(0)
			.setLayerCount(1));

	cmd->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, barrier, vk::DependencyFlagBits::eByRegion);

	Texture2D::BlitImageAsync(cmd, *renderTarget->finalColorBuffer, swapchainImage, scr_width, scr_height);

	vk::ImageMemoryBarrier presentBarrier;
	presentBarrier
		.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
		.setNewLayout(vk::ImageLayout::ePresentSrcKHR)
		.setImage(swapchainImage)
		.setSubresourceRange(vk::ImageSubresourceRange()
			.setAspectMask(vk::ImageAspectFlagBits::eColor)
			.setBaseMipLevel(0)
			.setLevelCount(1)
			.setBaseArrayLayer(0)
			.setLayerCount(1));

	cmd->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, presentBarrier, vk::DependencyFlagBits::eByRegion);

	auto fence = std::make_shared<VKWrapper::VKFence>(VKCONTEXT->GetDevice().get());
	CmdSyncSeamphore syncData{ .waitSemaphores = {{imageAcquiredSemaphore}}, .signalSemaphores = {SignalSemaphoreData{.semaphore = renderFinishedSemaphore}} };
	VKCONTEXT->SubmitCommandImmediately(cmd, syncData, fence);
	_vulkanSwapchain->PresentImage(*renderFinishedSemaphore, imageIndex);
	fence->Wait();

}

void VulkanRenderer::SetupRenderState(std::shared_ptr<RenderState>& state)
{
	auto cmd = VKCONTEXT->GetCommandBuffer();

	state->camera.prevUBO = std::make_shared<UniformBlock>(sizeof(camera_compData));
	state->camera.curUBO = std::make_shared<UniformBlock>(sizeof(camera_compData));

	state->camera.prevUBO->WriteDataAsync(cmd, &_cameraCache, sizeof(camera_compData));

	camera_compData cameraData;
	cameraData.projection = state->camera.projection;
	cameraData.view = state->camera.view;
	cameraData.projView = cameraData.projection * cameraData.view;
	cameraData.invProjection = glm::inverse(state->camera.projection);
	cameraData.invView = glm::inverse(state->camera.view);
	cameraData.invProjView = glm::inverse(cameraData.projView);
	glm::mat4 invTrans = glm::mat3(glm::transpose(cameraData.invView));
	cameraData.invTransViewRow1 = invTrans[0];
	cameraData.invTransViewRow2 = invTrans[1];
	cameraData.invTransViewRow3 = invTrans[2];
	cameraData.position = state->camera.position;
	cameraData.direction = state->camera.direction;
	cameraData.directionUp = state->camera.directionUp;
	cameraData.directionRight = state->camera.directionRight;
	cameraData.nearPlane = state->camera.nearPlane;
	cameraData.farPlane = state->camera.farPlane;
	cameraData.fov = state->camera.fov;
	_cameraCache = cameraData;

	state->camera.curUBO->WriteDataAsync(cmd, &cameraData, sizeof(camera_compData));
	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

	state->framebuffer.width = scr_width;
	state->framebuffer.height = scr_height;

	state->renderRecord.prevEV100 = _record.prevEV100;
	state->renderRecord.prevRenderMicroTimeStamp = _record.prevRenderMicroTimeStamp;
	state->renderRecord.currentRenderMicroTimeStamp = Tool::GetTimestampMircoseconds();
}

void VulkanRenderer::SetupIndirectDrawData(std::shared_ptr<RenderState>& state)
{

	auto bindlessTextureManager = BindlessTextureManager::Instance();
	auto indirectManager = IndirectDrawManager::Instance();

	{
		auto& items = state->objects.sceneRenderData.opaqueMesh;
		for (auto& item : items)
		{
			auto& material = item.meshinfo.material;
			if (material->GetNeedUpdateIndirectDraw())
				indirectManager->SetupMaterial(*material);

			auto& mesh = item.meshinfo.mesh;
			if (mesh->GetNeedUpdateIndricetDraw())
			{
				indirectManager->SetupMesh(*mesh);
				mesh->SetNeedUpdateIndirectDraw(false);
			}
		}
	}

	{
		auto& items = state->objects.sceneRenderData.transparentMesh;
		for (auto& item : items)
		{
			auto& material = item.meshinfo.material;
			if (material->GetNeedUpdateIndirectDraw())
				indirectManager->SetupMaterial(*material);

			auto& mesh = item.meshinfo.mesh;
			if (mesh->GetNeedUpdateIndricetDraw())
			{
				indirectManager->SetupMesh(*mesh);
				mesh->SetNeedUpdateIndirectDraw(false);
			}
		}
	}

	{
		auto& items = state->objects.sceneRenderData.opaqueSkinnedModel;
		for (auto& item : items)
		{
			for (auto& info : item.models)
			{
				auto& material = info.material;
				if (material->GetNeedUpdateIndirectDraw())
					indirectManager->SetupMaterial(*material);

				auto& mesh = info.mesh;
				if (mesh->GetNeedUpdateIndricetDraw())
				{
					indirectManager->SetupMesh(*mesh);
					mesh->SetNeedUpdateIndirectDraw(false);
				}
			}
		}
	}

	{
		auto& items = state->objects.sceneRenderData.transparentSkinnedMesh;
		for (auto& item : items)
		{
			auto& material = item.meshinfo.material;
			if (material->GetNeedUpdateIndirectDraw())
				indirectManager->SetupMaterial(*material);

			auto& mesh = item.meshinfo.mesh;
			if (mesh->GetNeedUpdateIndricetDraw())
			{
				indirectManager->SetupMesh(*mesh);
				mesh->SetNeedUpdateIndirectDraw(false);
			}
		}
	}
}

void VulkanRenderer::FinishRendering(std::shared_ptr<RenderState>& state)
{
	_record.prevEV100 = state->option.postProcessParams.EV100;
	_record.prevRenderMicroTimeStamp = state->renderRecord.currentRenderMicroTimeStamp;
}

bool VulkanRenderer::PushFrameState(std::shared_ptr<RenderState>& state)
{
	if (!state)
		return;
	LockGuard guard(_candidateFrameStatesMutex);
	if (_candidateFrameStates.size() >= _maxFramesInFlight)
		_candidateFrameStates.pop();
	_candidateFrameStates.push(state);
}

void VulkanRenderer::WaitImage(std::function<void(std::shared_ptr<Texture2D>)> callback)
{
	while (!FetchImage(callback)) {
		std::this_thread::yield();
	}
}
bool VulkanRenderer::FetchImage(std::function<void(std::shared_ptr<Texture2D>)> callback)
{
	if (_doneFrames.empty())
		return false;

	{
		LockGuard guard(_doneFramesMutex);
		if (!_doneFrames.empty())
		{
			if (callback
				&& _doneFrames.front()
				&& _doneFrames.front()->renderTarget
				&& _doneFrames.front()->renderTarget->finalColorBuffer
				)
				callback(_doneFrames.front()->renderTarget->finalColorBuffer);
			_doneFrames.pop();
			return true;
		}
		return false;
	}
}

void VulkanRenderer::Run()
{
	if (!_stop)
		return;

	_frameTaskPool.start();
	_stop = false;
	_loopThread = std::make_shared<std::thread>(&VulkanRenderer::ExecuteLoop, this);
}

void VulkanRenderer::Stop()
{
	if (_stop)
		return;

	_stop = true;
	_frameTaskPool.stop();
	if (_loopThread)
	{
		if (_loopThread->joinable())
			_loopThread->join();
		_loopThread.reset();
	}
}

void VulkanRenderer::ExecuteLoop()
{
	while (!_stop)
	{
		//auto time = Tool::GetTimestampSecond();
		//if (time - _lastCleanupTimeAccumulator > _CleanupThresold)
		//{
		//	if (_lastCleanupTimeAccumulator != 0)
		//		_resManager.CleanupIdleResource();
		//	_lastCleanupTimeAccumulator = Tool::GetTimestampSecond();
		//}


		//if (_needsCompile)
		//{
		//	Compile();
		//}

		bool emptyLoop = true;

		{
			LockGuard guard(_candidateFrameStatesMutex);
			while (!_candidateFrameStates.empty() && (_runningFrames.size() < _maxFramesInFlight))
			{
				auto sync = std::make_shared<RenderGraph::Graph::FilghtSync>();
				sync->curframeTimeLine = std::make_shared<VKWrapper::VKTimelineSemaphore>(VKCONTEXT->GetDevice().get());
				sync->preFrameTimeLine = !_runningFrames.empty() ? _runningFrames.back()->sync->curframeTimeLine : nullptr;

				auto data = std::make_shared<FrameData>();
				data->sync = std::move(sync);
				data->state = std::move(_candidateFrameStates.front());
				data->state->renderRecord.frameIndex = _record.frameIndex++;
				data->state->option = _option;
				data->renderTarget = _renderTargets[data->state->renderRecord.frameIndex % _renderTargets.size()];
				data->handle = _frameTaskPool.submit([&, weakData = std::weak_ptr(data)]()->void
					{
						if (auto data = weakData.lock())
							DrawOffScreen(data);
					});
				_candidateFrameStates.pop();
				_runningFrames.push(std::move(data));
				emptyLoop = false;
			}
		}

		while (!_runningFrames.empty() && _runningFrames.front()->handle->is_ready())
		{
			auto data = std::move(_runningFrames.front());
			_runningFrames.pop();
			if (_mode == RenderMode::Present)
				PresentImage(data);

			LockGuard guard(_doneFramesMutex);
			if (_doneFrames.size() >= _maxFramesInFlight)
				_doneFrames.pop();
			_doneFrames.push(std::move(data));
			emptyLoop = false;
		}

		if (emptyLoop)
			std::this_thread::yield();
	}
}
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
#include "VulkanRenderEngine/RenderPass/AtomspherePass.h"

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

		_renderTargets.push_back(std::move(renderTarget));
	}

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
	RenderGraph::RenderGraphResource CreateTexture(uint32_t width, uint32_t height,
		vk::Format format, vk::Filter filter, vk::SamplerAddressMode wrap,
		const RenderGraph::ResourceName& name,
		uint32_t maxLevel = 1) {
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
	static auto MakeConbinIndirectLightingPass = []()-> auto {

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

		return MakeLambdaPass(
			[_shader = makeCombinShader("shader/postprocess/combin.comp"), work_size_x = work_size_x, work_size_y = work_size_y]
			(RenderGraph::PassFrameCmdContext& cmdCtx, const RenderGraph::PassFrameContext& ctx, RenderState& state) mutable -> void
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

				auto cmd = cmdCtx.GetCmd();

				if (!Texture2D::CopyTextureAsync(cmd, sceneColorBuffer, tempColorBuffer))
					return;

				cmd->SubmitToQueue();

				all_tex.push_back(tempColorBuffer);

				uint32_t width = sceneColorBuffer->GetWidth();
				uint32_t height = sceneColorBuffer->GetHeight();

				ComputeBindingRecord binding;

				binding.SetStorageImage(sceneColorBuffer, vk::ImageAspectFlagBits::eColor, 0);
				binding.SetUniformTextureArray(all_tex, vk::ImageAspectFlagBits::eColor, 2);

				uint32_t count = (uint32_t)all_tex.size();

				_shader->Bind(cmd, binding);
				_shader->SetPushConstants(cmd, &count, sizeof(count));

				cmd->dispatch((width + work_size_x - 1) / work_size_x, (height + work_size_y - 1) / work_size_y, 1);

				cmd->SubmitToQueue();
			});
		};


	uint32_t width = scr_width;
	uint32_t height = scr_height;

	_sceneRenderGraph = std::make_unique<RenderGraph::Graph>("SceneRenderGraph");

	auto preCalculatePass = std::make_unique<PreCalculatePass>();

	auto hzbPass = std::make_unique<HZBPass>(
		"shader/HZB/depth.vs",
		"shader/HZB/depth.fs",
		"shader/HZB/HZBGenerate.comp",
		"shader/HZB/occlusionCulling.comp"
	);

	auto lightingShadowDepthPass = std::make_unique<LightShadowDepthPass>(
		"shader/lighting/AMDViewport_Dirlightshadow_StaticMesh.vs",
		"shader/lighting/AMDViewport_Dirlightshadow_Skinned.vs",
		"shader/lighting/AMDViewport_Dirlightshadow.fs",
		"shader/lighting/AMDViewport_Pointlightshadow_StaticMesh.vs",
		"shader/lighting/AMDViewport_Pointlightshadow_Skinned.vs",
		"shader/lighting/AMDViewport_Pointlightshadow.fs"
	);

	auto geometryPass = std::make_unique<GeometryPass>(
		"shader/gbuffer/geometrypass_StaticMesh.vs",
		"shader/gbuffer/geometrypass_StaticMesh.fs",
		"shader/gbuffer/geometrypass_SkinnedMesh.vs",
		"shader/gbuffer/geometrypass_SkinnedMesh.fs");

	auto ssaoPass = std::make_unique<SSAOPass>("shader/ssao/ssao.comp", "shader/ssao/ssaoblur.comp");

	auto lightingPass = std::make_unique<LightingPass>("shader/lighting/lightingpass.comp");

	auto copyDepthPass = MakeLambdaPass([](RenderGraph::PassFrameCmdContext& cmdCtx, const RenderGraph::PassFrameContext& ctx, RenderState& state)-> void
		{
			auto cmd = cmdCtx.GetCmd();
			auto geometryDepthStencil = ctx.GetInput(0);
			auto renderTargetDepthBuffer = ctx.GetExternal(0);
			Texture2D::CopyTextureAsync(cmd, geometryDepthStencil, renderTargetDepthBuffer);
			cmd->SubmitToQueue();
		});

	auto skyBoxPass = std::make_unique<SkyBoxPass>("shader/skybox/skybox.comp");

	std::unique_ptr<RenderPassBase> generalPass;
	std::unique_ptr<RenderPassBase> reflectPass;
	std::unique_ptr<RenderPassBase> giPass;

	if (!GlobalConfig::RTCoreEnable)
	{
		auto t_generalPass = std::make_unique<RayTraceGeneralPass>();
		auto t_reflectPass = std::make_unique<RayTraceReflectPass>("shader/RayTrace/RayTraceReflect.comp", "shader/RayTrace/BilateralFilterBlur.comp", "shader/RayTrace/TemporalAccumulate.comp", "shader/General/imagescale.comp");
		auto t_giPass = std::make_unique<RayTraceGIPass>("shader/RayTrace/RayTraceGI.comp", "shader/RayTrace/BilateralFilterBlur.comp", "shader/RayTrace/TemporalAccumulate.comp", "shader/General/imagescale.comp");

		t_reflectPass->SetGeneralBuffer(t_generalPass->GetGeneralBuffer());
		t_giPass->SetGeneralBuffer(t_generalPass->GetGeneralBuffer());

		generalPass = std::move(t_generalPass);
		reflectPass = std::move(t_reflectPass);
		giPass = std::move(t_giPass);
	}
	else
	{

		auto t_generalPass = std::make_unique<RTCoreRayTraceGeneralPass>();
		auto t_reflectPass = std::make_unique<RTCoreRayTraceReflectPass>(
			"shader/RTCoreRayTrace/RayTraceReflect.rgen", "shader/RTCoreRayTrace/Miss.rmiss", "shader/RTCoreRayTrace/ClosestHit.rchit",
			"", "", "",
			"shader/RayTrace/BilateralFilterBlur.comp", "shader/RayTrace/TemporalAccumulate.comp", "shader/General/imagescale.comp");
		auto t_giPass = std::make_unique<RTCoreRayTraceGIPass>(
			"shader/RTCoreRayTrace/RayTraceGI.rgen", "shader/RTCoreRayTrace/Miss.rmiss", "shader/RTCoreRayTrace/ClosestHit.rchit",
			"", "", "",
			"shader/RayTrace/BilateralFilterBlur.comp", "shader/RayTrace/TemporalAccumulate.comp", "shader/General/imagescale.comp");

		t_reflectPass->SetGeneralBuffer(t_generalPass->GetGeneralBuffer());
		t_giPass->SetGeneralBuffer(t_generalPass->GetGeneralBuffer());

		generalPass = std::move(t_generalPass);
		reflectPass = std::move(t_reflectPass);
		giPass = std::move(t_giPass);
	}

	auto ssrPass = std::make_unique<SSRPass>("shader/ssr/SSReflect.comp", "shader/ssr/BilateralFilterBlur.comp", "shader/ssr/TemporalAccumulate.comp");

	auto ssgiPass = std::make_unique<SSGIPass>("shader/ssr/SSGI.comp", "shader/ssr/BilateralFilterBlur.comp", "shader/ssr/TemporalAccumulate.comp");

	auto combinIndirectLightingPass = MakeConbinIndirectLightingPass();

	auto lightDrawPass = std::make_unique<LightDrawPass>("shader/lighting/lightMesh.vs", "shader/lighting/lightMesh.fs");

	//auto effectPass = std::make_unique<EffectPass>("shader/effect/effectpass.vs", "shader/effect/effectpass.fs");

	//auto transparentPass = std::make_unique<TransparentPass>("shader/Transparent/transparentpass.vs", "shader/Transparent/transparentpass.fs")

	auto atomspherePass = std::make_unique<AtomspherePass>("shader/Atomsphere/Atomsphere.comp", "shader/Atomsphere/TransmittanceLut.comp", "shader/Atomsphere/SkyViewLut.comp");

	auto depthFogPass = std::make_unique<DepthFogPass>("shader/postprocess/depthFog.comp");

	auto autoExposurePass = std::make_unique<AutoExposurePass>("shader/AutoExposure/histogram.comp");


	auto& sceneColorBuffer = _renderTargets[0]->sceneColorBuffer;
	auto& sceneDepthBuffer = _renderTargets[0]->sceneDepthBuffer;

	auto Ext_RenderTargetColorBuffer = _sceneRenderGraph->CreateExternalTexture(Ext_RenderTargetColorBuffer_Name);
	auto Ext_RenderTargetDepthBuffer = _sceneRenderGraph->CreateExternalTexture(Ext_RenderTargetDepthBuffer_Name);

	ResourceBuilder resbuilder(_sceneRenderGraph);

	auto hzbMap = resbuilder.CreateTexture(width, height, vk::Format::eD32Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "HZBMap", hzbPass->GetMaxLevel());
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
	auto preCalculateNode = _sceneRenderGraph->AddNode("preCalculateNode");
	auto hzbNode = _sceneRenderGraph->AddNode("hzbNode");
	auto geometryNode = _sceneRenderGraph->AddNode("geometry");
	auto lightingShadowDepthNode = _sceneRenderGraph->AddNode("lightingShadowDepthNode");
	auto ssaoNode = _sceneRenderGraph->AddNode("ssaoNode");
	auto lightingNode = _sceneRenderGraph->AddNode("lightingNode");
	auto copyDepthNode = _sceneRenderGraph->AddNode("copyDepthNode");
	auto skyBoxNode = _sceneRenderGraph->AddNode("skyBoxNode");
	auto rayTraceGeneralNode = _sceneRenderGraph->AddNode("rayTraceGeneralNode");
	auto rayTraceReflectNode = _sceneRenderGraph->AddNode("rayTraceReflectNode");
	auto rayTraceGINode = _sceneRenderGraph->AddNode("rayTraceGINode");
	auto ssrNode = _sceneRenderGraph->AddNode("ssrNode");
	auto ssgiNode = _sceneRenderGraph->AddNode("ssgiNode");
	auto combinIndirectLightingNode = _sceneRenderGraph->AddNode("combinIndirectLightingNode");
	auto lightDrawNode = _sceneRenderGraph->AddNode("lightDrawNode");
	auto opaqueFence = _sceneRenderGraph->AddFence("opaqueFence");

	// 透明物体
	auto effectNode = _sceneRenderGraph->AddNode("effectNode");
	auto transparentNode = _sceneRenderGraph->AddNode("transparentNode");
	auto transprantFence = _sceneRenderGraph->AddFence("transprantFence");
	transprantFence->After(opaqueFence);

	// 后处理
	auto depthFogNode = _sceneRenderGraph->AddNode("depthFogNode");
	auto atomsphereNode = _sceneRenderGraph->AddNode("atomsphereNode");
	auto postProcessFence = _sceneRenderGraph->AddFence("postProcessFence");
	postProcessFence->After(transprantFence);

	// 曝光计算
	auto autoExposureNode = _sceneRenderGraph->AddNode("autoExposureNode");

	preCalculateNode->SetRenderPass(std::move(preCalculatePass));


	using RenderGraphResource = RenderGraph::RenderGraphResource;
	using TextureLayout = RenderGraph::TextureLayout;
	using RenderGraphResourceLayout = RenderGraph::RenderGraphResourceLayout;
	using ResourceData = RenderGraph::PassNode::ResourceData;
	using ExternalResourceData = RenderGraph::PassNode::ExternalResourceData;

	auto computeReadLayout = RenderGraph::RenderGraphResourceLayout{ .data = TextureLayout{.stage = ImageLayout::BindStage::Compute, .usage = ImageLayout::BindUsage::Read} };
	auto computeWriteLayout = RenderGraph::RenderGraphResourceLayout{ .data = TextureLayout{.stage = ImageLayout::BindStage::Compute, .usage = ImageLayout::BindUsage::Write} };
	auto graphicsReadLayout = RenderGraph::RenderGraphResourceLayout{ .data = TextureLayout{.stage = ImageLayout::BindStage::Graphics, .usage = ImageLayout::BindUsage::Read} };
	auto graphicsWriteLayout = RenderGraph::RenderGraphResourceLayout{ .data = TextureLayout{.stage = ImageLayout::BindStage::Graphics, .usage = ImageLayout::BindUsage::Write} };
	auto transferReadLayout = RenderGraph::RenderGraphResourceLayout{ .data = TextureLayout{.stage = ImageLayout::BindStage::Transfer, .usage = ImageLayout::BindUsage::Read} };
	auto transferWriteLayout = RenderGraph::RenderGraphResourceLayout{ .data = TextureLayout{.stage = ImageLayout::BindStage::Transfer, .usage = ImageLayout::BindUsage::Write} };
	auto rayTracingReadLayout = RenderGraph::RenderGraphResourceLayout{ .data = TextureLayout{.stage = ImageLayout::BindStage::RayTracing, .usage = ImageLayout::BindUsage::Read} };
	auto rayTracingWriteLayout = RenderGraph::RenderGraphResourceLayout{ .data = TextureLayout{.stage = ImageLayout::BindStage::RayTracing, .usage = ImageLayout::BindUsage::Write} };

	hzbNode->SetRenderPass(std::move(hzbPass))
		.Temp(ResourceData{ resbuilder.CreateTexture(width, height, vk::Format::eD32Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "hzbPass_temp"), graphicsWriteLayout })
		.Output(ResourceData{ hzbMap , computeWriteLayout })
		.After(preCalculateNode)
		.Before(geometryNode);

	lightingShadowDepthNode->SetRenderPass(std::move(lightingShadowDepthPass))
		.Output(ResourceData{ atlasShadowMap ,graphicsWriteLayout })
		.After(preCalculateNode)
		.Before(lightingNode);

	geometryNode->SetRenderPass(std::move(geometryPass))
		.Output(
			ResourceData{ gPosition ,graphicsWriteLayout },
			ResourceData{ gNormal ,graphicsWriteLayout },
			ResourceData{ gAlbedoOpacity ,graphicsWriteLayout },
			ResourceData{ gMetallicRoughnessMap ,graphicsWriteLayout },
			ResourceData{ gMotionVectorMap ,graphicsWriteLayout },
			ResourceData{ gEmission ,graphicsWriteLayout },
			ResourceData{ gDepthStencilMap ,graphicsWriteLayout }
		)
		.After(hzbNode, preCalculateNode)
		.Before(lightingNode);

	ssaoNode->SetRenderPass(std::move(ssaoPass))
		.Input(ResourceData{ gPosition ,computeReadLayout }, ResourceData{ gNormal,computeReadLayout })
		.Temp(ResourceData{ resbuilder.CreateTexture(ssaoOutPut, "ssaoColorBuffer"), computeWriteLayout })
		.Output(ResourceData{ ssaoOutPut, computeWriteLayout })
		.After(geometryNode)
		.Before(lightingNode);

	lightingNode->SetRenderPass(std::move(lightingPass))
		.Input(
			ResourceData{ gPosition, computeReadLayout },
			ResourceData{ gNormal, computeReadLayout },
			ResourceData{ gAlbedoOpacity, computeReadLayout },
			ResourceData{ gMetallicRoughnessMap, computeReadLayout },
			ResourceData{ atlasShadowMap, computeReadLayout },
			ResourceData{ ssaoOutPut, computeReadLayout },
			ResourceData{ gEmission, computeReadLayout },
			ResourceData{ gDepthStencilMap, computeReadLayout }
		)
		.External(ExternalResourceData{ Ext_RenderTargetColorBuffer, computeWriteLayout })
		.After(lightingShadowDepthNode, ssaoNode)
		.Before(opaqueFence);

	copyDepthNode->SetRenderPass(std::move(copyDepthPass))
		.Input(ResourceData{ gDepthStencilMap, transferReadLayout })
		.External(ExternalResourceData{ Ext_RenderTargetDepthBuffer, transferWriteLayout })
		.Before(skyBoxNode, opaqueFence);

	skyBoxNode->SetRenderPass(std::move(skyBoxPass))
		.External(ExternalResourceData{ Ext_RenderTargetColorBuffer, computeWriteLayout }, ExternalResourceData{ Ext_RenderTargetDepthBuffer, computeReadLayout })
		.After(copyDepthNode)
		.Before(opaqueFence);

	rayTraceGeneralNode->SetRenderPass(std::move(generalPass));

	rayTraceReflectNode->SetRenderPass(std::move(reflectPass))
		.Input(
			ResourceData{ gPosition, computeReadLayout }, ResourceData{ gNormal, computeReadLayout }, ResourceData{ gAlbedoOpacity, computeReadLayout },
			ResourceData{ gMetallicRoughnessMap, computeReadLayout }, ResourceData{ atlasShadowMap, computeReadLayout }, ResourceData{ ssaoOutPut,computeReadLayout },
			ResourceData{ gMotionVectorMap, computeReadLayout }
		)
		.Temp(
			ResourceData{ resbuilder.CreateTexture(rayTraceReflect_Output, "rayTraceReflect_Temp1"), computeWriteLayout },
			ResourceData{ resbuilder.CreateTexture(rayTraceReflect_Output, "rayTraceReflect_Temp2"), computeWriteLayout }
		)
		.External(ExternalResourceData{ Ext_RenderTargetDepthBuffer, computeReadLayout })
		.Output(ResourceData{ rayTraceReflect_Output, computeWriteLayout })
		.Persistent(ResourceData{ resbuilder.CreateTexture(rayTraceReflect_Output, "rayTraceReflect_historyColorTexture"), computeReadLayout })
		.After(copyDepthNode, rayTraceGeneralNode)
		.Before(opaqueFence);

	rayTraceGINode->SetRenderPass(std::move(giPass))
		.Input(
			ResourceData{ gPosition, computeReadLayout }, ResourceData{ gNormal, computeReadLayout }, ResourceData{ gAlbedoOpacity,computeReadLayout },
			ResourceData{ gMetallicRoughnessMap, computeReadLayout }, ResourceData{ atlasShadowMap, computeReadLayout }, ResourceData{ ssaoOutPut, computeReadLayout },
			ResourceData{ gMotionVectorMap, computeReadLayout }
		)
		.Temp(
			ResourceData{ resbuilder.CreateTexture(rayTraceGI_Output, "rayTraceGI_Temp1"), computeWriteLayout },
			ResourceData{ resbuilder.CreateTexture(rayTraceGI_Output, "rayTraceGI_Temp2"), computeWriteLayout }
		)
		.External(ExternalResourceData{ Ext_RenderTargetDepthBuffer, computeReadLayout })
		.Output(ResourceData{ rayTraceGI_Output, computeWriteLayout })
		.Persistent(ResourceData{ resbuilder.CreateTexture(rayTraceGI_Output, "rayTraceGI_historyColorTexture"), computeReadLayout })
		.After(copyDepthNode, rayTraceGeneralNode)
		.Before(opaqueFence);

	ssrNode->SetRenderPass(std::move(ssrPass))
		.Input(
			ResourceData{ gPosition, computeReadLayout }, ResourceData{ gNormal, computeReadLayout }, ResourceData{ gAlbedoOpacity, computeReadLayout },
			ResourceData{ gMetallicRoughnessMap, computeReadLayout }, ResourceData{ gMotionVectorMap, computeReadLayout }, ResourceData{ hzbMap, computeReadLayout }
		)
		.Temp(
			ResourceData{ resbuilder.CreateTexture(ssr_Output, "ssrPass_temp1"), computeWriteLayout },
			ResourceData{ resbuilder.CreateTexture(ssr_Output, "ssrPass_temp2"), computeWriteLayout }
		)
		.External(ExternalResourceData{ Ext_RenderTargetColorBuffer, computeReadLayout }, ExternalResourceData{ Ext_RenderTargetDepthBuffer, computeReadLayout })
		.Output(ResourceData{ ssr_Output, computeWriteLayout })
		.Persistent(ResourceData{ resbuilder.CreateTexture(ssr_Output, "ssrPass_historyColorTexture"), computeReadLayout })
		.After(copyDepthNode, lightingNode)
		.Before(opaqueFence);

	ssgiNode->SetRenderPass(std::move(ssgiPass))
		.Input(
			ResourceData{ gPosition, computeReadLayout }, ResourceData{ gNormal, computeReadLayout }, ResourceData{ gAlbedoOpacity, computeReadLayout },
			ResourceData{ gMetallicRoughnessMap, computeReadLayout }, ResourceData{ ssaoOutPut, computeReadLayout }, ResourceData{ gMotionVectorMap, computeReadLayout },
			ResourceData{ hzbMap, computeReadLayout }
		)
		.Temp(
			ResourceData{ resbuilder.CreateTexture(ssgi_Output, "ssgiPass_temp1"), computeWriteLayout },
			ResourceData{ resbuilder.CreateTexture(ssgi_Output, "ssgiPass_temp2"), computeWriteLayout }
		)
		.External(ExternalResourceData{ Ext_RenderTargetColorBuffer, computeReadLayout }, ExternalResourceData{ Ext_RenderTargetDepthBuffer, computeReadLayout })
		.Output(ResourceData{ ssgi_Output, computeWriteLayout })
		.Persistent(ResourceData{ resbuilder.CreateTexture(ssgi_Output, "ssgiPass_historyColorTexture"), computeReadLayout })
		.After(copyDepthNode, lightingNode)
		.Before(opaqueFence);

	{

		combinIndirectLightingNode->SetRenderPass(std::move(combinIndirectLightingPass))
			// 输入选项也改为 ResourceData 形式，便于后续扩展格式说明
			.InputOption(
				ResourceData{ rayTraceReflect_Output, computeReadLayout }, ResourceData{ rayTraceGI_Output, computeReadLayout },
				ResourceData{ ssr_Output, computeReadLayout }, ResourceData{ ssgi_Output, computeReadLayout }
			)
			.External(ExternalResourceData{ Ext_RenderTargetColorBuffer, transferReadLayout })
			.Temp(ResourceData{ resbuilder.CreateTexture(sceneColorBuffer, "combinIndirectLightingPass_temp1"), transferWriteLayout })
			.After(rayTraceReflectNode, rayTraceGINode, ssrNode, ssgiNode)
			.Before(opaqueFence);
	}

	lightDrawNode->SetRenderPass(std::move(lightDrawPass))
		.External(ExternalResourceData{ Ext_RenderTargetColorBuffer, graphicsWriteLayout }, ExternalResourceData{ Ext_RenderTargetDepthBuffer, graphicsWriteLayout })
		.After(combinIndirectLightingNode)
		.Before(opaqueFence);

	//effectNode->SetRenderPass(std::move(effectPass));
	//	.After(opaqueFence)
	//	.Before(transprantFence);

	//transparentNode->SetRenderPass(std::move(transparentPass))
	//	.Input(atlasShadowMap)
	//	.After(opaqueFence, effectPass)
	//	.Before(transprantFence);

	atomsphereNode->SetRenderPass(std::move(atomspherePass))
		.After(opaqueFence, transprantFence)
		.External(ExternalResourceData{ Ext_RenderTargetColorBuffer, computeWriteLayout }, ExternalResourceData{ Ext_RenderTargetDepthBuffer, computeReadLayout })
		.Temp(ResourceData{ resbuilder.CreateTexture(sceneColorBuffer, "atomsphereNode_TempColor"), {} })
		.Before(postProcessFence);

	depthFogNode->SetRenderPass(std::move(depthFogPass))
		.After(atomsphereNode, opaqueFence, transprantFence)
		.External(ExternalResourceData{ Ext_RenderTargetColorBuffer, computeWriteLayout }, ExternalResourceData{ Ext_RenderTargetDepthBuffer, computeReadLayout })
		.Temp(ResourceData{ resbuilder.CreateTexture(sceneColorBuffer, "depthFogPass_TempColor"), {} })
		.Before(postProcessFence);

	autoExposureNode->SetRenderPass(std::move(autoExposurePass))
		.After(postProcessFence)
		.External(ExternalResourceData{ Ext_RenderTargetColorBuffer, computeReadLayout });

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
	cmd->SubmitNow(syncData, fence);
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
}

void VulkanRenderer::SetupIndirectDrawData(std::shared_ptr<RenderState>& state)
{
	std::unordered_set<Material*> willUpdateMaterials;
	std::unordered_set<Mesh*> willUpdateMeshs;

	auto indirectManager = IndirectDrawManager::Instance();

	{
		auto& items = state->objects.sceneRenderData.opaqueMesh;
		for (auto& item : items)
		{
			auto& mesh = item.meshinfo.mesh;
			auto& material = item.meshinfo.material;

			if (material->GetNeedUpdateIndirectDraw())
				willUpdateMaterials.insert(material.get());
			if (mesh->GetNeedUpdateIndirectDraw())
				willUpdateMeshs.insert(mesh.get());
		}
	}

	{
		auto& items = state->objects.sceneRenderData.transparentMesh;
		for (auto& item : items)
		{
			auto& material = item.meshinfo.material;
			auto& mesh = item.meshinfo.mesh;

			if (material->GetNeedUpdateIndirectDraw())
				willUpdateMaterials.insert(material.get());
			if (mesh->GetNeedUpdateIndirectDraw())
				willUpdateMeshs.insert(mesh.get());
		}
	}

	{
		auto& items = state->objects.sceneRenderData.opaqueSkinnedModel;
		for (auto& item : items)
		{
			for (auto& info : item.models)
			{
				auto& material = info.material;
				auto& mesh = info.mesh;

				if (material->GetNeedUpdateIndirectDraw())
					willUpdateMaterials.insert(material.get());
				if (mesh->GetNeedUpdateIndirectDraw())
					willUpdateMeshs.insert(mesh.get());
			}
		}
	}

	{
		auto& items = state->objects.sceneRenderData.transparentSkinnedMesh;
		for (auto& item : items)
		{
			auto& material = item.meshinfo.material;
			auto& mesh = item.meshinfo.mesh;

			if (material->GetNeedUpdateIndirectDraw())
				willUpdateMaterials.insert(material.get());
			if (mesh->GetNeedUpdateIndirectDraw())
				willUpdateMeshs.insert(mesh.get());
		}
	}

	if (!willUpdateMeshs.empty())
	{
		indirectManager->WithMeshWriteLock([&]() {
			std::for_each(std::execution::seq, willUpdateMeshs.begin(), willUpdateMeshs.end(),
				[&](auto& meshptr) {
					if (meshptr->GetNeedUpdateIndirectDraw())
					{
						indirectManager->SetupMesh_LockFree(*meshptr);
						meshptr->SetNeedUpdateIndirectDraw(false);
					}
				});
			});
	}

	if (!willUpdateMaterials.empty())
	{
		indirectManager->WithMaterialWriteLock([&]() {
			std::for_each(std::execution::seq, willUpdateMaterials.begin(), willUpdateMaterials.end(),
				[&](auto& materialptr) {
					if (materialptr->GetNeedUpdateIndirectDraw())
					{
						indirectManager->SetupMaterial_LockFree(*materialptr);
						materialptr->SetNeedUpdateIndirectDraw(false);
					}
				});
			});
	}
}

void VulkanRenderer::FinishRendering(std::shared_ptr<RenderState>& state)
{
	_record.prevEV100 = state->option.postProcessParams.EV100;
	state->renderRecord.frameEndMicroTimeStamp = Tool::GetTimestampMircoseconds();
}

void VulkanRenderer::PushFrameState(std::shared_ptr<RenderState>& state)
{
	if (!state)
		return;

	{
		LockGuard guard(_candidateFrameStatesMutex);
		if (_candidateFrameStates.size() >= _maxFramesInFlight)
			_candidateFrameStates.pop();
		_candidateFrameStates.push(state);
	}

	LockGuard guard(_executeMutex);
	_executeCV.NotifyOne();
}

void VulkanRenderer::WaitImage(const std::function<void(std::shared_ptr<Texture2D>, std::shared_ptr<RenderState>)>& callback)
{
	do
	{
		LockGuard guard(_doneFramesMutex);
		if (FetchImage(callback))
			return;

		_doneFramesCV.Wait(guard);
		if (_stop)
			return;
		if (FetchImage(callback))
			return;
	} while (true);
}

bool VulkanRenderer::FetchImage(const std::function<void(std::shared_ptr<Texture2D>, std::shared_ptr<RenderState>)>& callback)
{
	LockGuard guard(_doneFramesMutex);
	if (_doneFrames.empty())
		return false;

	if (callback
		&& _doneFrames.front()
		&& _doneFrames.front()->renderTarget
		&& _doneFrames.front()->renderTarget->finalColorBuffer
		)
	{
		auto& framedata = _doneFrames.front();
		callback(framedata->renderTarget->finalColorBuffer, framedata->state);
	}
	_doneFrames.pop();
	return true;
}

void VulkanRenderer::Run()
{
	if (!_stop)
		return;

	_frameTaskPool.start();
	_stop = false;
	_exeLoopThread = std::make_shared<std::thread>(&VulkanRenderer::ExecuteLoop, this);
}

void VulkanRenderer::Stop()
{
	if (_stop)
		return;

	_stop = true;
	_frameTaskPool.stop();

	{
		LockGuard guard(_doneFramesMutex);
		_doneFramesCV.NotifyAll();
	}

	{
		LockGuard guard(_executeMutex);
		_executeCV.NotifyAll();
	}

	if (_exeLoopThread)
	{
		if (_exeLoopThread->joinable())
			_exeLoopThread->join();
		_exeLoopThread.reset();
	}
}

void VulkanRenderer::ExecuteLoop()
{
	while (!_stop || !_runningFrames.empty())
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

		bool didWork = false;

		if (!_stop)
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
				data->state->renderRecord.frameStartMicroTimeStamp = Tool::GetTimestampMircoseconds();
				data->state->option = _option;
				data->renderTarget = _renderTargets[data->state->renderRecord.frameIndex % _renderTargets.size()];
				data->handle = _frameTaskPool.submit([&, weakData = std::weak_ptr(data)]()->void
					{
						if (auto data = weakData.lock())
							DrawOffScreen(data);

						LockGuard guard(_executeMutex);
						_executeCV.NotifyOne();
					});
				_candidateFrameStates.pop();
				_runningFrames.push(std::move(data));
				didWork = true;
			}
		}

		while (!_runningFrames.empty() && _runningFrames.front()->handle->is_ready())
		{
			auto data = std::move(_runningFrames.front());
			_runningFrames.pop();
			if (_mode == RenderMode::Present)
				PresentImage(data);

			{
				LockGuard guard(_doneFramesMutex);
				if (_doneFrames.size() >= _maxFramesInFlight)
					_doneFrames.pop();
				_doneFrames.push(std::move(data));
				_doneFramesCV.NotifyAll();
			}

			didWork = true;
		}


		if (!didWork)
		{
			LockGuard guard(_executeMutex);
			_executeCV.WaitFor(guard, std::chrono::milliseconds(20));
		}

	}
}

uint32_t VulkanRenderer::GetMaxFramesInFlight() const {
	return _maxFramesInFlight;
}
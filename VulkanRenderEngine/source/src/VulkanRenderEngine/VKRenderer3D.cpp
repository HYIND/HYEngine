#include "vkstdafx.h"
#include "VulkanRenderEngine/VKRenderer3D.h"

#include "VulkanRenderEngine/Base/GraphicsPipeline.h"

#include "VulkanRenderEngine/RenderPass/GeometryPass.h"
#include "VulkanRenderEngine/RenderPass/LightShadowDepthPass.h"
#include "VulkanRenderEngine/RenderPass/LightingPass.h"
#include "VulkanRenderEngine/RenderPass/LightDrawPass.h"
#include "VulkanRenderEngine/RenderPass/SSRPass.h"
#include "VulkanRenderEngine/RenderPass/SkyBoxPass.h"
#include "VulkanRenderEngine/RenderPass/SSAOPass.h"
#include "VulkanRenderEngine/RenderPass/EffectPass.h"
#include "VulkanRenderEngine/Base/Effect.h"
#include "VulkanRenderEngine/Base/Particle.h"
#include "VulkanRenderEngine/RenderPass/TransparentPass.h"
#include "VulkanRenderEngine/RenderPass/RayTraceGeneralPass.h"
#include "VulkanRenderEngine/RenderPass/RayTraceReflectPass.h"
#include "VulkanRenderEngine/RenderPass/RayTraceGIPass.h"
#include "VulkanRenderEngine/RenderPass/AutoExposurePass.h"
#include "VulkanRenderEngine/RenderPass/SSGIPass.h"
#include "VulkanRenderEngine/RenderPass/HZBPass.h"
#include "VulkanRenderEngine/RenderPass/PreCalculatePass.h"
#include "VulkanRenderEngine/RenderPass/AtmospherePreCalculatePass.h"
#include "VulkanRenderEngine/RenderPass/CloudPreCalculatePass.h"
#include "VulkanRenderEngine/RenderPass/SkyIBLDiffusePreCalculatePass.h"
#include "VulkanRenderEngine/RenderPass/IBLPreCalculatePass.h"
#include "VulkanRenderEngine/RenderPass/VolumeCalculatePass.h"

#include "VulkanRenderEngine/RenderPass/RTCoreRayTraceGeneralPass.h"
#include "VulkanRenderEngine/RenderPass/RTCoreRayTraceGIPass.h"
#include "VulkanRenderEngine/RenderPass/RTCoreRayTraceReflectPass.h"
#include "VulkanRenderEngine/RenderPass/DDGIProbeUpdatePass.h"
#include "VulkanRenderEngine/RenderPass/DDGIBlendPass.h"
#include "VulkanRenderEngine/RenderPass/DDGIApplyPass.h"
#include "VulkanRenderEngine/RenderPass/DDGIProbeDrawPass.h"
#include "VulkanRenderEngine/RenderPass/EditorPickPass.h"
#include "VulkanRenderEngine/RenderPass/EditorSelectionMaskPass.h"
#include "VulkanRenderEngine/General/EditorViewport.h"
#include "VulkanRenderEngine/General/DDGIDef.h"

#include <chrono>
#include <optional>

const std::string Ext_RenderTargetColorBuffer_Name = "renderTargetColorBuffer";
// 编辑器选中遮罩（RenderTargetData::editorSelectionMask，每个在途帧一张）
const std::string Ext_EditorSelectionMask_Name = "editorSelectionMask";

// ---------------------------------------------------------------------------------------------------
// Pass 并行构造
// Pass 的构造函数里主要是 shader 编译（shaderc）和管线创建，彼此独立，可以并发：
//   * SPIR-V 内存缓存、队列提交、描述符池分配、RenderGraph 资源管理都有锁；
//   * 命令池是 thread_local 的（VKThreadContext），每个工作线程各用各的；
//   * 构造期间的纹理上传（SSAO 噪声图等）都是提交并等待完成的，线程池销毁、工作线程退出时
//     各自的命令池随之释放，不会有还在 GPU 上执行的命令。
// 有顺序依赖的（DDGI 图集布局）和 Pass 之间的连接（SetGeneralBuffer / SetSharedState）仍在调用线程上做。
// 不用流水线的帧线程池：它在 Run() 里才启动，Resize 时也会被 Stop()。
// ---------------------------------------------------------------------------------------------------
namespace
{
	template <typename T>
	using PassTask = std::shared_ptr<ThreadPool::SubmitHandle<std::unique_ptr<T>>>;

	class PassConstructPool
	{
	public:
		PassConstructPool()
			: _pool(std::max(1u, std::thread::hardware_concurrency()))
		{
			_pool.start();
		}
		~PassConstructPool()
		{
			if (_pool.running())
				_pool.stop();
		}
		PassConstructPool(const PassConstructPool&) = delete;
		PassConstructPool& operator=(const PassConstructPool&) = delete;

		// 任务只能按值捕获：get() 抛异常时调用方会提前离开作用域
		template <typename F>
		auto Submit(F&& f) { return _pool.submit(std::forward<F>(f)); }

	private:
		ThreadPool _pool;
	};

	int64_t ElapsedMs(std::chrono::steady_clock::time_point start)
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
	}
}

VKRenderer3D::VKRenderer3D()
{
	scr_width = 0;
	scr_height = 0;
}

VKRenderer3D::~VKRenderer3D()
{}

void VKRenderer3D::OnResize(uint32_t width, uint32_t height, uint32_t flightCount)
{
	// 第一次：建尺寸无关的 Pass（只建一次）+ 中间目标 + 渲染图；
	// 之后只重建和分辨率有关的：中间目标、按新尺寸重建渲染图（复用 Pass，不重新编译 shader）
	scr_width = std::max(1u, width);
	scr_height = std::max(1u, height);
	_targetCount = std::max(1u, flightCount);

	Init_Internal();
	InitRenderGraph();
}

void VKRenderer3D::OnTick()
{
	if (_sceneRenderGraph)
		_sceneRenderGraph->CleanGraphIdleResource();
}

void VKRenderer3D::Init_Internal()
{
	// 与分辨率无关，只创建一次；互相独立，并行构造（见 PassConstructPool）
	{
		PassConstructPool pool;

		PassTask<BloomPass> bloomTask;
		PassTask<SkinnedPass> skinnedTask;
		PassTask<CombinPass> combinTask;
		PassTask<GlobalPostProcessPass> postProcessTask;
		PassTask<PreCalculatePass> preCalculateTask;

		if (!_globalBloomPass)
			bloomTask = pool.Submit([] { return std::make_unique<BloomPass>("shader/bloom/bloomDownSample.comp", "shader/bloom/bloomUpSample.comp"); });
		if (!_skinnedPass)
			skinnedTask = pool.Submit([] { return std::make_unique<SkinnedPass>(); });
		if (!_combinPass)
			combinTask = pool.Submit([] { return std::make_unique<CombinPass>("shader/postprocess/combin.comp"); });
		if (!_globalPostProcessPass)
			postProcessTask = pool.Submit([] { return std::make_unique<GlobalPostProcessPass>("shader/postprocess/globalpostprocess.comp"); });
		if (!_preCalculatePass)
			preCalculateTask = pool.Submit([] { return std::make_unique<PreCalculatePass>(); });

		if (bloomTask) _globalBloomPass = bloomTask->get();
		if (skinnedTask) _skinnedPass = skinnedTask->get();
		if (combinTask) _combinPass = combinTask->get();
		if (postProcessTask) _globalPostProcessPass = postProcessTask->get();
		if (preCalculateTask) _preCalculatePass = preCalculateTask->get();
	}

	// 编辑器：选中描边（后处理之后画在最终画面上）。游戏里不创建
	if (GlobalConfig::EditorViewportFeatures && !_editorOutlinePass)
		_editorOutlinePass = std::make_unique<EditorOutlinePass>();

	InitRenderTarget();
}

void VKRenderer3D::InitRenderTarget()
{
	_renderTargets.clear();

	const TextureConfig config
	{
		.minFilter = vk::Filter::eLinear,
		.magFilter = vk::Filter::eLinear,
		.wrapU = vk::SamplerAddressMode::eClampToEdge,
		.wrapV = vk::SamplerAddressMode::eClampToEdge,
		.anisotropy = false,
		.gammaCorrection = false
	};

	// bloom 链级数：每级减半，最后一级的长边不小于 16，最多 6 级、至少 1 级。
	// 用整数算：原来的 floor(log2(max(w, h) / 16)) + 1 在长边 < 16 时得 0 或负数，
	// 0 级时后面取 bloomMipBuffers[0] 越界，负数转 uint32_t 是未定义行为
	const uint32_t bloomMaxDim = std::max(scr_width, scr_height);
	uint32_t bloomLevel = 1;
	while (bloomLevel < 6u && (bloomMaxDim >> bloomLevel) >= 16u)
		++bloomLevel;

	for (uint32_t i = 0; i < _targetCount; i++)
	{
		auto renderTarget = std::make_shared<RenderTargetData>();

		renderTarget->sceneColorBuffer = std::make_shared<Texture2D>(scr_width, scr_height, vk::Format::eR16G16B16A16Sfloat, config);

		renderTarget->firstPersonColorBuffer = std::make_shared<Texture2D>(scr_width, scr_height, vk::Format::eR16G16B16A16Sfloat, config);

		renderTarget->combinColorBuffer = std::make_shared<Texture2D>(scr_width, scr_height, vk::Format::eR16G16B16A16Sfloat, config);
		renderTarget->combinBrightColorBuffer = std::make_shared<Texture2D>(scr_width, scr_height, vk::Format::eR16G16B16A16Sfloat, config);

		for (uint32_t level = 0; level < bloomLevel; level++)
		{
			uint32_t currW = std::max(1u, scr_width >> level);
			uint32_t currH = std::max(1u, scr_height >> level);
			renderTarget->bloomMipBuffers.push_back(std::make_shared<Texture2D>(currW, currH, vk::Format::eR16G16B16A16Sfloat, config));
		}

		// 编辑器选中遮罩：只用 texelFetch 读，过滤方式无所谓
		if (GlobalConfig::EditorViewportFeatures)
			renderTarget->editorSelectionMask = std::make_shared<Texture2D>(scr_width, scr_height, EditorSelectionMaskPass::GetMaskFormat(), config);

		_renderTargets.push_back(std::move(renderTarget));
	}

}

std::shared_ptr<DDGIDebugChannel> VKRenderer3D::GetDDGIDebugChannel()
{
	if (!_ddgiDebugChannel)
		_ddgiDebugChannel = std::make_shared<DDGIDebugChannel>();
	return _ddgiDebugChannel;
}

glm::u32vec2 VKRenderer3D::GetSize() const {
	return glm::u32vec2{ scr_width,scr_height };
}

uint32_t VKRenderer3D::GetWidth() const
{
	return scr_width;
}

uint32_t VKRenderer3D::GetHeight() const
{
	return scr_height;
}

Scene3DRenderOption VKRenderer3D::GetOption() const
{
	return _option;
}

void VKRenderer3D::SetOption(Scene3DRenderOption option)
{
	_option = option;
}

void VKRenderer3D::InitRenderGraph()
{
	InitSceneRenderGraph();
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
		uint32_t maxLevel = 1)
	{
		return _graph->CreateTexture(RenderGraph::TextureDesc{
			.width = width, .height = height, .format = format,
			.minFilter = minFilter, .magFilter = magFilter,
			.wrapU = wrapU, .wrapV = wrapV,
			.maxLevel = std::max(1u,maxLevel) }, name);
	}
	RenderGraph::RenderGraphResource CreateTexture(
		uint32_t width, uint32_t height,
		vk::Format format,
		vk::Filter filter,
		vk::SamplerAddressMode wrap,
		const RenderGraph::ResourceName& name,
		uint32_t maxLevel = 1) {
		return _graph->CreateTexture(RenderGraph::TextureDesc{
			.width = width, .height = height, .format = format,
			.minFilter = filter, .magFilter = filter,
			.wrapU = wrap, .wrapV = wrap,
			.maxLevel = std::max(1u,maxLevel) }, name);
	}

	RenderGraph::RenderGraphResource CreateTexture(
		glm::u32vec2 size,
		vk::Format format,
		vk::Filter minFilter, vk::Filter magFilter,
		vk::SamplerAddressMode wrapU, vk::SamplerAddressMode wrapV,
		const RenderGraph::ResourceName& name,
		uint32_t maxLevel = 1)
	{
		return CreateTexture(size.x, size.y, format, minFilter, magFilter, wrapU, wrapV, name, maxLevel);
	}
	RenderGraph::RenderGraphResource CreateTexture(
		glm::u32vec2 size,
		vk::Format format,
		vk::Filter filter,
		vk::SamplerAddressMode wrap,
		const RenderGraph::ResourceName& name,
		uint32_t maxLevel = 1)
	{
		return CreateTexture(size.x, size.y, format, filter, wrap, name, maxLevel);
	}

	// 3D 体积纹理（depth > 1）
	RenderGraph::RenderGraphResource CreateVolume(
		uint32_t width, uint32_t height, uint32_t depth,
		vk::Format format,
		vk::Filter filter,
		vk::SamplerAddressMode wrap,
		const RenderGraph::ResourceName& name,
		uint32_t maxLevel = 1) {
		return _graph->CreateTexture(RenderGraph::TextureDesc{
			.width = width, .height = height, .depth = std::max(1u, depth), .format = format,
			.minFilter = filter, .magFilter = filter,
			.wrapU = wrap, .wrapV = wrap, .wrapW = wrap,
			.maxLevel = std::max(1u,maxLevel) }, name);
	}

	RenderGraph::RenderGraphResource CreateVolume(
		glm::u32vec3 size,
		vk::Format format,
		vk::Filter filter,
		vk::SamplerAddressMode wrap,
		const RenderGraph::ResourceName& name,
		uint32_t maxLevel = 1)
	{
		return CreateVolume(size.x, size.y, size.z, format, filter, wrap, name, maxLevel);
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
		return _graph->CreateTexture(RenderGraph::TextureDesc{
			.width = 1, .height = 1, .format = format,
			.minFilter = filter, .magFilter = filter,
			.wrapU = wrap, .wrapV = wrap,
			.maxLevel = std::max(1u,maxLevel), .isVariable = true }, name);
	}

private:
	std::unique_ptr<RenderGraph::Graph>& _graph;
};

void VKRenderer3D::InitSceneRenderGraph()
{
	static auto MakeConbinIndirectLightingPass = []()-> auto {

		auto makeCombinShader = [&](const std::string& path) -> std::shared_ptr<ComputePipeline>
			{
				auto shader = std::make_shared<ComputePipeline>();

				ComputePipelineConfig config;
				config.AddDefineMacro("COMBIN_MODE", 1);
				config.AddDefineMacro("SkipBrightOutput", "");
				config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
				config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
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
			[_shader = makeCombinShader("shader/postprocess/combin.comp")]
			(RenderGraph::PassFrameCmdContext& cmdCtx, const RenderGraph::PassFrameContext& ctx, RenderState& state) mutable -> void
			{
				if (!_shader)
					return;

				auto sceneColorBuffer = ctx.GetExternal(0);
				if (!sceneColorBuffer)
					return;

				auto cmd = cmdCtx.GetCmd();

				std::vector<std::shared_ptr<Texture2D>> all_tex;
				const auto& denoiseDebug = state.option.denoiseDebug;
				if (state.option.DenoiseDebugEffective())
				{
					// 降噪调试：场景颜色只保留被调试 Pass 所在的分支（输入顺序：漫反射、镜面、路径追踪），
					// 不叠加原场景颜色。同分支另一个 Pass（RTR 与 SSR、RTGI 与 SSGI）开着时它的结果仍会叠加
					// 被调试的 Pass 没开时不走这里（分支输出是清零的，替换后整屏发黑），按正常画面合成
					using Target = Scene3DRenderOption::DenoiseDebugTarget;
					const bool specular = denoiseDebug.target == Target::RayTraceReflect || denoiseDebug.target == Target::SSR;
					if (auto tex = ctx.GetInput(specular ? 1 : 2))
						all_tex.push_back(tex);
					if (all_tex.empty())
						return;
				}
				else
				{
					for (auto* slots : { &ctx.inputs, &ctx.optionInputs }) {
						for (auto& slot : *slots) {
							if (auto tex = slot.As<Texture2D>()) all_tex.push_back(tex);
						}
					}
					if (all_tex.empty())
						return;

					auto tempColorBuffer = ctx.GetTemp(0);
					if (!Texture2D::CopyTextureAsync(cmd, sceneColorBuffer, tempColorBuffer))
						return;

					cmd->SubmitToQueue();

					all_tex.push_back(tempColorBuffer);
				}

				uint32_t width = sceneColorBuffer->GetWidth();
				uint32_t height = sceneColorBuffer->GetHeight();

				ComputeBindingRecord binding;

				binding.SetStorageImage(sceneColorBuffer, vk::ImageAspectFlagBits::eColor, 0);
				binding.SetUniformTextureArray(all_tex, vk::ImageAspectFlagBits::eColor, 2);

				uint32_t count = (uint32_t)all_tex.size();

				_shader->Bind(cmd, binding);
				_shader->SetPushConstants(cmd, &count, sizeof(count));

				cmd->dispatch((width + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (height + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

				cmd->SubmitToQueue();
			});
		};


	// 间接光分支合成：把同一维度（漫反射 / 镜面）里本帧执行了的 Pass 输出加法合成到该分支的输出。
	// 可选输入只会拿到本帧真正执行过的 Pass 的输出（没执行的 Pass 不会分配输出纹理）；
	// 一个都没有时把输出清零（瞬态纹理来自纹理池，内容未定义）。
	static auto MakeIndirectBranchCombinPass = []()-> auto {

		auto shader = std::make_shared<ComputePipeline>();
		{
			ComputePipelineConfig config;
			config.AddDefineMacro("COMBIN_MODE", 1);
			config.AddDefineMacro("SkipBrightOutput", "");
			config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
			config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
			config.computePath = "shader/postprocess/combin.comp";

			config
				.AddStorageImage(0)
				.AddStorageImage(1)
				.AddUnifromVariableTextureArray(2, 20)
				.AddPushConstant(sizeof(uint32_t));

			if (config.Validate())
				shader->Create(config);
		}

		return MakeLambdaPass(
			[_shader = shader]
			(RenderGraph::PassFrameCmdContext& cmdCtx, const RenderGraph::PassFrameContext& ctx, RenderState& state) mutable -> void
			{
				auto outImage = ctx.GetOutput(0);
				if (!_shader || !outImage || outImage->IsEmpty())
					return;

				std::vector<std::shared_ptr<Texture2D>> all_tex;
				for (auto& slot : ctx.optionInputs) {
					if (auto tex = slot.As<Texture2D>()) all_tex.push_back(tex);
				}

				auto cmd = cmdCtx.GetCmd();
				outImage->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

				if (all_tex.empty())
				{
					vk::ClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
					vk::ImageSubresourceRange range;
					range.setAspectMask(vk::ImageAspectFlagBits::eColor)
						.setBaseArrayLayer(0)
						.setLayerCount(1)
						.setBaseMipLevel(0)
						.setLevelCount(1);
					cmd->clearColorImage(outImage->GetImage(), vk::ImageLayout::eGeneral, clearColor, range);
					outImage->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
					cmd->SubmitToQueue();
					return;
				}

				ComputeBindingRecord binding;
				binding.SetStorageImage(outImage, vk::ImageAspectFlagBits::eColor, 0);
				binding.SetUniformTextureArray(all_tex, vk::ImageAspectFlagBits::eColor, 2);

				uint32_t count = (uint32_t)all_tex.size();
				_shader->Bind(cmd, binding);
				_shader->SetPushConstants(cmd, &count, sizeof(count));

				auto size = outImage->GetSize();
				cmd->dispatch((size.x + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (size.y + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

				cmd->SubmitToQueue();
			});
		};

	uint32_t width = scr_width;
	uint32_t height = scr_height;

	// 按新尺寸重建时旧图先留着：新图建好后把尺寸无关的持久资源（DDGI 图集等）连同内容接手过来，再释放旧图
	auto oldGraph = std::move(_sceneRenderGraph);
	_sceneRenderGraph = std::make_unique<RenderGraph::Graph>("SceneRenderGraph");

	// Pass 只在第一次构建渲染图时创建（shader 都在构造函数里编译）；resize 重建渲染图时复用同一批 Pass 对象，
	// 只按新尺寸重新声明一组资源。Pass 之间的连接（共享的光追 buffer、DDGI 共享状态）也随对象保留
	const bool rebuild = _scenePasses != nullptr;
	if (!_scenePasses)
	{
		const auto constructStart = std::chrono::steady_clock::now();

		// ---- 1. 在本线程先做：有顺序依赖的、Pass 之间共享的 ----

		// DDGI 图集布局取决于设备的纹理尺寸上限，必须在创建 DDGI 管线（注入布局宏）之前确定
		{
			auto device = VKCONTEXT->GetDevice();
			const auto& limits = device->GetPhysicalDeviceProperties().properties.limits;
			const auto physicalDevice = device->GetPhysicalDevice();
			const auto r11Props = physicalDevice.getFormatProperties(vk::Format::eB10G11R11UfloatPack32);
			const bool r11Storage = physicalDevice.getFeatures().shaderStorageImageExtendedFormats
				&& (r11Props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eStorageImage)
				&& (r11Props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear);
			DDGIAtlasLayout::Configure(limits.maxImageDimension2D, r11Storage);
			const auto& layout = DDGIAtlasLayout::Get();
			std::cout << std::format("[ DDGI ] capacity={} budget={} probesPerRow={} rows={} irradiance={}\n",
				layout.capacity, layout.budget, layout.probesPerRow, layout.rows, layout.irradianceR11 ? "R11G11B10" : "RGBA16F");
		}
		if (!_ddgiDebugChannel)
			_ddgiDebugChannel = std::make_shared<DDGIDebugChannel>();

		// 描述符池是懒创建的，外层的空判断不是原子的：先在本线程建好，免得多个线程同时进入
		VKCONTEXT->GetDescriptorPool();

		// DDGI Pass 共享跨帧状态，保证同一帧的体积布局、更新列表逐位一致
		auto ddgiSharedState = std::make_shared<DDGISharedState>(_ddgiDebugChannel);
		// 体积拆成 Lut（提前算散射 / 透射率）和 Combine（不透明完成后合成），两者共享本帧的 Lut 是否有效
		auto volumeShared = std::make_shared<VolumePassShared>();

		// ---- 2. 并行构造（按值捕获；管线多的先提交） ----
		PassConstructPool pool;

		auto effectTask = pool.Submit([] { return std::make_unique<EffectPass>("shader/effect/particle.vs", "shader/effect/particle_color.fs", "shader/effect/particle_material.fs"); });

		auto transparentTask = pool.Submit([] {
			return std::make_unique<TransparentPass>(
				"shader/Transparent/transparent_static.vs", "shader/Transparent/transparent_static.fs",
				"shader/Transparent/transparent_static.vs", "shader/Transparent/transparent_oit.fs",
				"shader/Transparent/transparent_oit_composite.vs", "shader/Transparent/transparent_oit_composite.fs");
			});

		auto lightingShadowDepthTask = pool.Submit([] {
			return std::make_unique<LightShadowDepthPass>(
				"shader/lighting/AMDViewport_Dirlightshadow_StaticMesh.vs",
				"shader/lighting/AMDViewport_Dirlightshadow.fs",
				"shader/lighting/AMDViewport_Pointlightshadow_StaticMesh.vs",
				"shader/lighting/AMDViewport_Pointlightshadow.fs");
			});

		auto hzbTask = pool.Submit([] {
			return std::make_unique<HZBPass>(
				"shader/HZB/depth.vs",
				"shader/HZB/depth.fs",
				"shader/HZB/HZBGenerate.comp",
				"shader/HZB/occlusionCulling.comp");
			});

		auto atmospherePreCalculateTask = pool.Submit([] {
			return std::make_unique<AtmospherePreCalculatePass>(
				"shader/Atmosphere/TransmittanceLut.comp",
				"shader/Atmosphere/MultiScatteringLut.comp",
				"shader/Atmosphere/SkyViewLut.comp",
				"shader/Atmosphere/SkyCubeGenerate.comp");
			});
		auto cloudPreCalculateTask = pool.Submit([] {
			return std::make_unique<CloudPreCalculatePass>("shader/Cloud/CloudCubeGenerate.comp");
			});
		auto skyIblDiffuseTask = pool.Submit([] {
			return std::make_unique<SkyIBLDiffusePreCalculatePass>();
			});

		// 光追三件套：General 只建几个空 buffer，留在本线程建（Reflect / GI / DDGIUpdate 要用它的 GeneralBuffer，连接在汇合后做）
		std::unique_ptr<RenderPassBase> generalPass;
		std::shared_ptr<RTCoreRayTraceGeneralBuffer> rtCoreGeneralBuffer;
		std::shared_ptr<RayTraceGeneralBuffer> rtGeneralBuffer;
		PassTask<RTCoreRayTraceReflectPass> rtCoreReflectTask;
		PassTask<RTCoreRayTraceGIPass> rtCoreGITask;
		PassTask<RayTraceReflectPass> rtReflectTask;
		PassTask<RayTraceGIPass> rtGITask;
		if (GlobalConfig::RTCoreEnable)
		{
			rtCoreReflectTask = pool.Submit([] {
				return std::make_unique<RTCoreRayTraceReflectPass>(
					"shader/RTCoreRayTrace/RayTraceReflect.rgen", "shader/RTCoreRayTrace/Miss.rmiss", "shader/RTCoreRayTrace/ClosestHit.rchit",
					"", "", "");
				});
			rtCoreGITask = pool.Submit([] {
				return std::make_unique<RTCoreRayTraceGIPass>(
					"shader/RTCoreRayTrace/RayTraceGI.rgen", "shader/RTCoreRayTrace/Miss.rmiss", "shader/RTCoreRayTrace/ClosestHit.rchit",
					"", "", "");
				});

			auto t_generalPass = std::make_unique<RTCoreRayTraceGeneralPass>();
			rtCoreGeneralBuffer = t_generalPass->GetGeneralBuffer();
			generalPass = std::move(t_generalPass);
		}
		else
		{
			rtReflectTask = pool.Submit([] { return std::make_unique<RayTraceReflectPass>("shader/RayTrace/RayTraceReflect.comp"); });
			rtGITask = pool.Submit([] { return std::make_unique<RayTraceGIPass>("shader/RayTrace/RayTraceGI.comp"); });

			auto t_generalPass = std::make_unique<RayTraceGeneralPass>();
			rtGeneralBuffer = t_generalPass->GetGeneralBuffer();
			generalPass = std::move(t_generalPass);
		}

		auto ddgiUpdateTask = pool.Submit([] {
			return std::make_unique<DDGIProbeUpdatePass>(
				"shader/DDGI/DDGIProbeUpdate.rgen", "shader/RTCoreRayTrace/Miss.rmiss", "shader/DDGI/DDGIClosestHit.rchit",
				"", "", "");
			});
		auto ddgiBlendTask = pool.Submit([] { return std::make_unique<DDGIBlendPass>("shader/DDGI/DDGIBlend.comp"); });
		auto ddgiApplyTask = pool.Submit([] { return std::make_unique<DDGIApplyPass>("shader/DDGI/DDGIApply.comp"); });
		auto ddgiProbeDrawTask = pool.Submit([] { return std::make_unique<DDGIProbeDrawPass>("shader/DDGI/DDGIProbeDraw.vs", "shader/DDGI/DDGIProbeDraw.fs"); });

		auto geometryTask = pool.Submit([] {
			return std::make_unique<GeometryPass>(
				"shader/gbuffer/geometrypass_StaticMesh.vs",
				"shader/gbuffer/geometrypass_StaticMesh.fs");
			});
		auto ssaoTask = pool.Submit([] { return std::make_unique<SSAOPass>("shader/ssao/ssao.comp", "shader/ssao/ssaoblur.comp"); });
		auto lightingTask = pool.Submit([] { return std::make_unique<LightingPass>("shader/lighting/lightingpass.comp"); });
		auto lightDrawTask = pool.Submit([] { return std::make_unique<LightDrawPass>("shader/lighting/lightMesh.vs", "shader/lighting/lightMesh.fs"); });
		auto skyBoxTask = pool.Submit([] { return std::make_unique<SkyBoxPass>("shader/skybox/skybox.comp"); });
		auto skyboxPreCalculateTask = pool.Submit([] { return std::make_unique<SkyBoxPreCalculatePass>(); });
		auto iblPreCalculateTask = pool.Submit([] {
			return std::make_unique<IBLPreCalculatePass>("shader/Cloud/CloudSkyComposite.comp");
			});
		auto ssrTask = pool.Submit([] { return std::make_unique<SSRPass>("shader/ssr/SSReflect.comp"); });
		auto ssgiTask = pool.Submit([] { return std::make_unique<SSGIPass>("shader/ssr/SSGI.comp"); });

		auto volumeLutTask = pool.Submit([volumeShared] { return std::make_unique<VolumeLutPass>("shader/Volume/VolumeLut.comp", volumeShared); });
		auto volumeCombineTask = pool.Submit([volumeShared] { return std::make_unique<VolumeCombinePass>("shader/Volume/VolumeCalculate.comp", volumeShared); });

		auto autoExposureTask = pool.Submit([] { return std::make_unique<AutoExposurePass>("shader/AutoExposure/histogram.comp"); });
		auto gBufferHistoryCopyTask = pool.Submit([] { return std::make_unique<GBufferHistoryCopyPass>(); });

		// 间接光合成：漫反射 / 镜面 / 路径追踪三路分别合成，最后加到场景颜色（分工见 IndirectLightingPolicy.h）
		auto indirectDiffuseCombinTask = pool.Submit([] { return MakeIndirectBranchCombinPass(); });
		auto indirectSpecularCombinTask = pool.Submit([] { return MakeIndirectBranchCombinPass(); });
		auto indirectPathTracingCombinTask = pool.Submit([] { return MakeIndirectBranchCombinPass(); });
		auto combinIndirectLightingTask = pool.Submit([] { return MakeConbinIndirectLightingPass(); });

		// ---- 3. 汇合 ----
		auto atmospherePreCalculatePass = atmospherePreCalculateTask->get();
		auto skyboxPreCalculatePass = skyboxPreCalculateTask->get();
		auto cloudPreCalculatePass = cloudPreCalculateTask->get();
		auto skyIblDiffusePass = skyIblDiffuseTask->get();
		auto iblPreCalculatePass = iblPreCalculateTask->get();
		auto hzbPass = hzbTask->get();
		auto lightingShadowDepthPass = lightingShadowDepthTask->get();
		auto geometryPass = geometryTask->get();
		auto ssaoPass = ssaoTask->get();
		auto lightingPass = lightingTask->get();
		auto skyBoxPass = skyBoxTask->get();
		auto ddgiUpdatePass = ddgiUpdateTask->get();
		auto ddgiBlendPass = ddgiBlendTask->get();
		auto ddgiApplyPass = ddgiApplyTask->get();
		auto ddgiProbeDrawPass = ddgiProbeDrawTask->get();
		auto ssrPass = ssrTask->get();
		auto ssgiPass = ssgiTask->get();
		auto indirectDiffuseCombinPass = indirectDiffuseCombinTask->get();
		auto indirectSpecularCombinPass = indirectSpecularCombinTask->get();
		auto indirectPathTracingCombinPass = indirectPathTracingCombinTask->get();
		auto combinIndirectLightingPass = combinIndirectLightingTask->get();
		auto lightDrawPass = lightDrawTask->get();
		auto volumeLutPass = volumeLutTask->get();
		auto volumeCombinePass = volumeCombineTask->get();
		auto transparentPass = transparentTask->get();
		auto effectPass = effectTask->get();
		auto autoExposurePass = autoExposureTask->get();
		auto gBufferHistoryCopyPass = gBufferHistoryCopyTask->get();

		// ---- 4. Pass 之间的连接（与原先串行构造时的顺序一致） ----
		ddgiUpdatePass->SetSharedState(ddgiSharedState);
		ddgiBlendPass->SetSharedState(ddgiSharedState);
		ddgiApplyPass->SetSharedState(ddgiSharedState);
		ddgiProbeDrawPass->SetSharedState(ddgiSharedState);

		std::unique_ptr<RenderPassBase> reflectPass;
		std::unique_ptr<RenderPassBase> giPass;
		if (GlobalConfig::RTCoreEnable)
		{
			auto t_reflectPass = rtCoreReflectTask->get();
			auto t_giPass = rtCoreGITask->get();

			t_reflectPass->SetGeneralBuffer(rtCoreGeneralBuffer);
			t_reflectPass->SetSharedState(ddgiSharedState);		// 反射命中点采样 DDGI
			t_giPass->SetGeneralBuffer(rtCoreGeneralBuffer);
			ddgiUpdatePass->SetGeneralBuffer(rtCoreGeneralBuffer);

			reflectPass = std::move(t_reflectPass);
			giPass = std::move(t_giPass);
		}
		else
		{
			auto t_reflectPass = rtReflectTask->get();
			auto t_giPass = rtGITask->get();

			t_reflectPass->SetGeneralBuffer(rtGeneralBuffer);
			t_giPass->SetGeneralBuffer(rtGeneralBuffer);

			reflectPass = std::move(t_reflectPass);
			giPass = std::move(t_giPass);
		}

		ssrPass->SetSharedState(ddgiSharedState);		// 反射命中点采样 DDGI

		std::cout << std::format("[ Renderer ] 场景 Pass 并行构造完成，耗时 {} ms\n", ElapsedMs(constructStart));

		auto passes = std::make_unique<ScenePasses>();
		passes->hzbMaxLevel = hzbPass->GetMaxLevel();
		passes->atmospherePreCalculate = std::move(atmospherePreCalculatePass);
		passes->skyboxPreCalculate = std::move(skyboxPreCalculatePass);
		passes->cloudPreCalculate = std::move(cloudPreCalculatePass);
		passes->skyIblDiffusePreCalculate = std::move(skyIblDiffusePass);
		passes->iblPreCalculate = std::move(iblPreCalculatePass);
		passes->hzb = std::move(hzbPass);
		passes->lightingShadowDepth = std::move(lightingShadowDepthPass);
		passes->geometry = std::move(geometryPass);
		passes->ssao = std::move(ssaoPass);
		passes->lighting = std::move(lightingPass);
		passes->skyBox = std::move(skyBoxPass);
		passes->rayTraceGeneral = std::move(generalPass);
		passes->rayTraceReflect = std::move(reflectPass);
		passes->rayTraceGI = std::move(giPass);
		passes->ssr = std::move(ssrPass);
		passes->ssgi = std::move(ssgiPass);
		passes->ddgiUpdate = std::move(ddgiUpdatePass);
		passes->ddgiBlend = std::move(ddgiBlendPass);
		passes->ddgiApply = std::move(ddgiApplyPass);
		passes->ddgiProbeDraw = std::move(ddgiProbeDrawPass);
		passes->indirectDiffuseCombin = std::move(indirectDiffuseCombinPass);
		passes->indirectSpecularCombin = std::move(indirectSpecularCombinPass);
		passes->indirectPathTracingCombin = std::move(indirectPathTracingCombinPass);
		passes->combinIndirectLighting = std::move(combinIndirectLightingPass);
		passes->lightDraw = std::move(lightDrawPass);
		passes->volumeLut = std::move(volumeLutPass);
		passes->volumeCombine = std::move(volumeCombinePass);
		passes->transparent = std::move(transparentPass);
		passes->effect = std::move(effectPass);
		passes->autoExposure = std::move(autoExposurePass);
		passes->gBufferHistoryCopy = std::move(gBufferHistoryCopyPass);

		// 编辑器视口：拾取（读回实体 ID 缓冲）。游戏里不创建
		if (GlobalConfig::EditorViewportFeatures)
		{
			auto editorPickPass = std::make_unique<EditorPickPass>();
			editorPickPass->SetChannel(_editorViewportChannel);
			passes->editorPick = std::move(editorPickPass);
			passes->editorSelectionMask = std::make_unique<EditorSelectionMaskPass>();
		}

		_scenePasses = std::move(passes);
	}
	auto& P = *_scenePasses;

	auto& sceneColorBuffer = _renderTargets[0]->sceneColorBuffer;

	auto Ext_RenderTargetColorBuffer = _sceneRenderGraph->CreateExternalTexture(Ext_RenderTargetColorBuffer_Name);

	ResourceBuilder resbuilder(_sceneRenderGraph);

	auto hzbMap = resbuilder.CreateTexture(width, height, vk::Format::eD32Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "HZBMap", P.hzbMaxLevel);
	auto hzbMinDepthMap = resbuilder.CreateTexture(width, height, vk::Format::eD32Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "HZBMinDepthMap", P.hzbMaxLevel);

	auto gPosition = resbuilder.CreateTexture(width, height, vk::Format::eR32G32B32A32Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "gPosition");
	auto gNormal = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "gNormal");
	auto gDepthStencil = resbuilder.CreateTexture(width, height, vk::Format::eD24UnormS8Uint, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "gDepthStencil");
	// 运动矢量只用 .xy、自发光只用 .rgb：格式由 GeometryPass 统一决定（管线的附件格式必须一致），设备不支持时回退 RGBA16F
	auto gMotionVector = resbuilder.CreateTexture(width, height, GeometryPass::GetMotionVectorFormat(), vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "gMotionVector");

	auto gAlbedoOpacity = resbuilder.CreateTexture(width, height, vk::Format::eR8G8B8A8Unorm, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "gAlbedoOpacity");
	auto gMetallicRoughness = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "gMetallicRoughness");
	auto gEmission = resbuilder.CreateTexture(width, height, GeometryPass::GetEmissionFormat(), vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "gEmission");

	// 编辑器：实体 ID 缓冲（GeometryPass 的第 7 个输出，存 entityId + 1，0 = 没有物体），给拾取读回。
	// 必须和 GeometryPass 管线有没有这个附件一致（两边都看 GlobalConfig::EditorViewportFeatures）
	std::optional<RenderGraph::RenderGraphResource> gEntityId;
	if (GeometryPass::HasEntityIdOutput())
		gEntityId = resbuilder.CreateTexture(width, height, GeometryPass::GetEntityIdFormat(), vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "gEntityId");

	auto ssaoOutPut = resbuilder.CreateTexture(width, height, vk::Format::eR8Unorm, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "ssaoOutPutBuffer");
	// RTR / RTGI：按 2/3 分辨率追踪、时域累积、空域滤波（rtLowRes 的 Temp / Persistent），
	// 最后一步联合双边上采样 + 材质重新调制写到全分辨率输出（IndirectUpsample），合成 Pass 直接读全分辨率结果
	const uint32_t rtLowResWidth = std::max(1u, (width * 2) / 3);
	const uint32_t rtLowResHeight = std::max(1u, (height * 2) / 3);
	auto rtLowRes = [&](const RenderGraph::ResourceName& name) {
		return resbuilder.CreateTexture(rtLowResWidth, rtLowResHeight, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eLinear, vk::SamplerAddressMode::eClampToEdge, name);
		};
	auto rayTraceReflect_Output = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "rayTraceReflect_Output");
	auto rayTraceGI_Output = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "rayTraceGI_Output");
	auto ssr_Output = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "ssr_Output");
	auto ssgi_Output = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "ssgi_Output");

	// DDGI 资源按探针池容量一次性分配（布局见 DDGIDef.h），rayData 只按每帧更新预算分配。
	// 四张图集都是持久资源：按世界格子滚动寻址、原地更新（探针更新读上一帧结果 → 混合写本帧结果 → 采样方读），没有 history。
	// 辐照度 / 深度图集必须 Linear 采样，Nearest 会让探针在方向上阶跃
	const auto ddgiRayDataSize = DDGIAtlasLayout::RayDataSize();
	const auto ddgiIrradianceSize = DDGIAtlasLayout::IrradianceAtlasSize();
	const auto ddgiDepthSize = DDGIAtlasLayout::DepthAtlasSize();
	const auto ddgiProbeTexelSize = DDGIAtlasLayout::ProbeTexelSize();
	auto ddgiRayData = resbuilder.CreateTexture(ddgiRayDataSize.x, ddgiRayDataSize.y, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "ddgiRayData");
	auto ddgiIrradianceAtlas = resbuilder.CreateTexture(ddgiIrradianceSize.x, ddgiIrradianceSize.y, DDGIAtlasLayout::IrradianceFormat(), vk::Filter::eLinear, vk::SamplerAddressMode::eClampToEdge, "ddgiIrradianceAtlas");
	auto ddgiDepthAtlas = resbuilder.CreateTexture(ddgiDepthSize.x, ddgiDepthSize.y, vk::Format::eR16G16Sfloat, vk::Filter::eLinear, vk::SamplerAddressMode::eClampToEdge, "ddgiDepthAtlas");
	// 每探针 1 texel：probeData = (偏移, 已更新次数 n + 背面比例)，probeStats = 突变检测统计量（只有 Blend 读写）
	auto ddgiProbeData = resbuilder.CreateTexture(ddgiProbeTexelSize.x, ddgiProbeTexelSize.y, vk::Format::eR32G32B32A32Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "ddgiProbeData");
	auto ddgiProbeStats = resbuilder.CreateTexture(ddgiProbeTexelSize.x, ddgiProbeTexelSize.y, vk::Format::eR32G32B32A32Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "ddgiProbeStats");
	auto ddgi_Output = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eLinear, vk::SamplerAddressMode::eClampToEdge, "ddgi_Output");
	// 间接光分支合成结果（全分辨率，已乘材质）：
	//   漫反射 = 纯漫反射方案（DDGI）；镜面 = 纯镜面方案（RTR + SSR）；路径追踪 = 漫反射 + 镜面都有的方案（RTGI + SSGI）
	auto indirectDiffuse_Output = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "indirectDiffuse_Output");
	auto indirectSpecular_Output = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "indirectSpecular_Output");
	auto indirectPathTracing_Output = resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "indirectPathTracing_Output");
	auto atlasShadowMap = resbuilder.CreateVariableTexture(vk::Format::eD32Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "atlasShadow");

	// 上一帧的 G-Buffer（持久资源）：RTR / RTGI / SSR / SSGI / VolumeLut 声明成 Persistent 直接读（追加在各自 Persistent 列表末尾），
	// 都读完之后由 gBufferHistoryCopyNode 把本帧的结果拷进来，给下一帧用
	auto gPositionHistory = resbuilder.CreateTexture(gPosition, "gPositionHistory");
	auto gNormalHistory = resbuilder.CreateTexture(gNormal, "gNormalHistory");
	auto gDepthStencilHistory = resbuilder.CreateTexture(gDepthStencil, "gDepthStencilHistory");


	// 不透明物体
	auto preCalculateNode = _sceneRenderGraph->AddNode("preCalculateNode");
	auto atmospherePreCalculateNode = _sceneRenderGraph->AddNode("atmospherePreCalculateNode");
	auto skyboxPreCalculateNode = _sceneRenderGraph->AddNode("skyboxPreCalculateNode");
	auto skyIblDiffusePreCalculateNode = _sceneRenderGraph->AddNode("skyIblDiffusePreCalculateNode");
	auto cloudPreCalculateNode = _sceneRenderGraph->AddNode("cloudPreCalculateNode");
	auto iblPreCalculateNode = _sceneRenderGraph->AddNode("iblPreCalculateNode");
	auto hzbNode = _sceneRenderGraph->AddNode("hzbNode");
	auto geometryNode = _sceneRenderGraph->AddNode("geometry");
	auto lightingShadowDepthNode = _sceneRenderGraph->AddNode("lightingShadowDepthNode");
	auto ssaoNode = _sceneRenderGraph->AddNode("ssaoNode");
	auto lightingNode = _sceneRenderGraph->AddNode("lightingNode");
	auto skyBoxNode = _sceneRenderGraph->AddNode("skyBoxNode");
	auto rayTraceGeneralNode = _sceneRenderGraph->AddNode("rayTraceGeneralNode");
	auto rayTraceReflectNode = _sceneRenderGraph->AddNode("rayTraceReflectNode");
	auto rayTraceGINode = _sceneRenderGraph->AddNode("rayTraceGINode");
	auto ssrNode = _sceneRenderGraph->AddNode("ssrNode");
	auto ssgiNode = _sceneRenderGraph->AddNode("ssgiNode");
	auto ddgiUpdateNode = _sceneRenderGraph->AddNode("ddgiUpdateNode");
	auto ddgiBlendNode = _sceneRenderGraph->AddNode("ddgiBlendNode");
	auto ddgiApplyNode = _sceneRenderGraph->AddNode("ddgiApplyNode");
	auto ddgiProbeDrawNode = _sceneRenderGraph->AddNode("ddgiProbeDrawNode");
	auto indirectDiffuseCombinNode = _sceneRenderGraph->AddNode("indirectDiffuseCombinNode");
	auto indirectSpecularCombinNode = _sceneRenderGraph->AddNode("indirectSpecularCombinNode");
	auto indirectPathTracingCombinNode = _sceneRenderGraph->AddNode("indirectPathTracingCombinNode");
	auto combinIndirectLightingNode = _sceneRenderGraph->AddNode("combinIndirectLightingNode");
	auto lightDrawNode = _sceneRenderGraph->AddNode("lightDrawNode");
	auto opaqueFence = _sceneRenderGraph->AddFence("opaqueFence");

	// 透明物体
	auto effectNode = _sceneRenderGraph->AddNode("effectNode");
	auto transparentNode = _sceneRenderGraph->AddNode("transparentNode");
	auto transprantFence = _sceneRenderGraph->AddFence("transprantFence");
	transprantFence->After(opaqueFence);

	// 体积：Lut 只依赖几何 / SSAO / 阴影 / 大气预计算，和光照那一段并行；Combine 在不透明完成后合成
	auto volumeLutNode = _sceneRenderGraph->AddNode("volumeLutNode");
	auto volumeCombineNode = _sceneRenderGraph->AddNode("volumeCombineNode");
	// G-Buffer 历史：读历史的节点都执行完后，把本帧的 gPosition / gNormal / gDepthStencil 拷进历史
	auto gBufferHistoryCopyNode = _sceneRenderGraph->AddNode("gBufferHistoryCopyNode");
	// 后处理
	auto postProcessFence = _sceneRenderGraph->AddFence("postProcessFence");
	postProcessFence->After(transprantFence);

	// 曝光计算
	auto autoExposureNode = _sceneRenderGraph->AddNode("autoExposureNode");


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


	atmospherePreCalculateNode->SetRenderPass(P.atmospherePreCalculate);

	skyboxPreCalculateNode->SetRenderPass(P.skyboxPreCalculate);

	// 原始天空 diffuse：排在背景源（大气 / 天空盒）之后、云之前——云的环境项从这里取
	skyIblDiffusePreCalculateNode->SetRenderPass(P.skyIblDiffusePreCalculate)
		.After(atmospherePreCalculateNode, skyboxPreCalculateNode);

	cloudPreCalculateNode->SetRenderPass(P.cloudPreCalculate)
		.After(atmospherePreCalculateNode, skyIblDiffusePreCalculateNode, lightingShadowDepthNode);

	iblPreCalculateNode->SetRenderPass(P.iblPreCalculate)
		.After(atmospherePreCalculateNode, skyboxPreCalculateNode, cloudPreCalculateNode, skyIblDiffusePreCalculateNode);

	hzbNode->SetRenderPass(P.hzb)
		.Temp(ResourceData{ resbuilder.CreateTexture(width, height, vk::Format::eD32Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "hzbPass_temp"), graphicsWriteLayout })
		.Output(ResourceData{ hzbMap, computeWriteLayout }, ResourceData{ hzbMinDepthMap, computeWriteLayout })
		.After(preCalculateNode)
		.Before(geometryNode);

	lightingShadowDepthNode->SetRenderPass(P.lightingShadowDepth)
		.Output(ResourceData{ atlasShadowMap ,graphicsWriteLayout })
		.After(preCalculateNode)
		.Before(lightingNode);

	geometryNode->SetRenderPass(P.geometry)
		.Output(
			ResourceData{ gPosition ,graphicsWriteLayout },
			ResourceData{ gNormal ,graphicsWriteLayout },
			ResourceData{ gAlbedoOpacity ,graphicsWriteLayout },
			ResourceData{ gMetallicRoughness ,graphicsWriteLayout },
			ResourceData{ gMotionVector ,graphicsWriteLayout },
			ResourceData{ gEmission ,graphicsWriteLayout }
		)
		.After(hzbNode, preCalculateNode)
		.Before(lightingNode)
		.FrameLocal(ResourceData{ gDepthStencil, graphicsWriteLayout });
	if (gEntityId)
	{
		geometryNode->Output(ResourceData{ *gEntityId, graphicsWriteLayout });		// Output 6
		// 透明物体写实体 ID 时用的临时深度：不透明深度的拷贝，透明物体在上面做深度测试并写入（gDepthStencil 不动）
		geometryNode->Temp(ResourceData{ resbuilder.CreateTexture(gDepthStencil, "geometry_editorTransparentDepth"), graphicsWriteLayout });	// Temp 0
	}

	ssaoNode->SetRenderPass(P.ssao)
		.Input(ResourceData{ gPosition ,computeReadLayout }, ResourceData{ gNormal,computeReadLayout })
		.Temp(ResourceData{ resbuilder.CreateTexture(ssaoOutPut, "ssaoColorBuffer"), computeWriteLayout })
		.Output(ResourceData{ ssaoOutPut, computeWriteLayout })
		.After(geometryNode)
		.Before(lightingNode)
		.FrameLocal(ResourceData{ gDepthStencil, graphicsWriteLayout });

	lightingNode->SetRenderPass(P.lighting)
		.Input(
			ResourceData{ gPosition, computeReadLayout },
			ResourceData{ gNormal, computeReadLayout },
			ResourceData{ gAlbedoOpacity, computeReadLayout },
			ResourceData{ gMetallicRoughness, computeReadLayout },
			ResourceData{ atlasShadowMap, computeReadLayout },
			ResourceData{ ssaoOutPut, computeReadLayout },
			ResourceData{ gEmission, computeReadLayout }
		)
		.External(ExternalResourceData{ Ext_RenderTargetColorBuffer, computeWriteLayout })
		.After(atmospherePreCalculateNode, skyboxPreCalculateNode, iblPreCalculateNode, lightingShadowDepthNode, ssaoNode)
		.Before(opaqueFence)
		.FrameLocal(ResourceData{ gDepthStencil, computeReadLayout });

	// 天空在间接光合成之后画：天空像素没有间接光，合成会把场景颜色整张拷出再写回，
	// 和天空并行执行时会把天空覆盖掉；场景颜色是外部资源，图里不会自动推出这条依赖
	skyBoxNode->SetRenderPass(P.skyBox)
		.External(ExternalResourceData{ Ext_RenderTargetColorBuffer, computeWriteLayout })
		.After(geometryNode, combinIndirectLightingNode)
		.Before(lightDrawNode, opaqueFence)
		.FrameLocal(ResourceData{ gDepthStencil ,computeReadLayout });

	rayTraceGeneralNode->SetRenderPass(P.rayTraceGeneral);

	rayTraceReflectNode->SetRenderPass(P.rayTraceReflect)
		.Input(
			ResourceData{ gPosition, computeReadLayout }, ResourceData{ gNormal, computeReadLayout }, ResourceData{ gAlbedoOpacity, computeReadLayout },
			ResourceData{ gMetallicRoughness, computeReadLayout }, ResourceData{ atlasShadowMap, computeReadLayout }, ResourceData{ ssaoOutPut,computeReadLayout },
			ResourceData{ gMotionVector, computeReadLayout }
		)
		.Temp(
			ResourceData{ rtLowRes("rayTraceReflect_Temp1"), computeWriteLayout },
			ResourceData{ rtLowRes("rayTraceReflect_Temp2"), computeWriteLayout },
			ResourceData{ rtLowRes("rayTraceReflect_Temp3"), computeWriteLayout },
			ResourceData{ rtLowRes("rayTraceReflect_Temp4"), computeWriteLayout },
			ResourceData{ rtLowRes("rayTraceReflect_Temp5"), computeWriteLayout },		// 空域滤波结果，上采样的输入
			ResourceData{ rtLowRes("rayTraceReflect_HitMotion"), computeWriteLayout }	// 第一次反弹命中点的运动（硬件光追）
		)
		// 命中点的漫反射间接光采样 DDGI（可选持久资源：DDGI 从没执行过时为空，不会因此分配）
		.PersistentOption(
			ResourceData{ ddgiIrradianceAtlas, computeReadLayout },
			ResourceData{ ddgiDepthAtlas, computeReadLayout },
			ResourceData{ ddgiProbeData, computeReadLayout }
		)
		.Output(ResourceData{ rayTraceReflect_Output, computeWriteLayout })
		.Persistent(
			ResourceData{ rtLowRes("rayTraceReflect_historyColorTexture"), computeReadLayout },
			ResourceData{ rtLowRes("rayTraceReflect_historyMomentTexture"), computeReadLayout },
			// 上一帧的 G-Buffer（Persistent 2 ~ 4）
			ResourceData{ gPositionHistory, computeReadLayout },
			ResourceData{ gNormalHistory, computeReadLayout },
			ResourceData{ gDepthStencilHistory, computeReadLayout }
		)
		.After(atmospherePreCalculateNode, skyboxPreCalculateNode, iblPreCalculateNode, lightingShadowDepthNode, geometryNode, rayTraceGeneralNode, ddgiBlendNode)
		.Before(opaqueFence)
		.FrameLocal(ResourceData{ gDepthStencil, computeReadLayout });

	rayTraceGINode->SetRenderPass(P.rayTraceGI)
		.Input(
			ResourceData{ gPosition, computeReadLayout }, ResourceData{ gNormal, computeReadLayout }, ResourceData{ gAlbedoOpacity,computeReadLayout },
			ResourceData{ gMetallicRoughness, computeReadLayout }, ResourceData{ atlasShadowMap, computeReadLayout }, ResourceData{ ssaoOutPut, computeReadLayout },
			ResourceData{ gMotionVector, computeReadLayout }
		)
		.Temp(
			ResourceData{ rtLowRes("rayTraceGI_Temp1"), computeWriteLayout },
			ResourceData{ rtLowRes("rayTraceGI_Temp2"), computeWriteLayout },
			ResourceData{ rtLowRes("rayTraceGI_Temp3"), computeWriteLayout },
			ResourceData{ rtLowRes("rayTraceGI_Temp4"), computeWriteLayout },
			ResourceData{ rtLowRes("rayTraceGI_Temp5"), computeWriteLayout },		// 空域滤波结果，上采样的输入
			// 路径追踪拆成两路：Temp 0 ~ 4 是漫反射，5 ~ 9 是镜面（顺序相同，见 RTCoreRayTraceGIPass::Execute）
			ResourceData{ rtLowRes("rayTraceGI_SpecTemp1"), computeWriteLayout },
			ResourceData{ rtLowRes("rayTraceGI_SpecTemp2"), computeWriteLayout },
			ResourceData{ rtLowRes("rayTraceGI_SpecTemp3"), computeWriteLayout },
			ResourceData{ rtLowRes("rayTraceGI_SpecTemp4"), computeWriteLayout },
			ResourceData{ rtLowRes("rayTraceGI_SpecTemp5"), computeWriteLayout },
			ResourceData{ rtLowRes("rayTraceGI_SpecHitMotion"), computeWriteLayout }	// 镜面那一路命中点的运动（硬件光追，Temp 10）
		)
		.Output(ResourceData{ rayTraceGI_Output, computeWriteLayout })
		.Persistent(
			ResourceData{ rtLowRes("rayTraceGI_historyColorTexture"), computeReadLayout },
			ResourceData{ rtLowRes("rayTraceGI_historyMomentTexture"), computeReadLayout },
			ResourceData{ rtLowRes("rayTraceGI_specHistoryColorTexture"), computeReadLayout },
			ResourceData{ rtLowRes("rayTraceGI_specHistoryMomentTexture"), computeReadLayout },
			// 上一帧的 G-Buffer（Persistent 4 ~ 6）
			ResourceData{ gPositionHistory, computeReadLayout },
			ResourceData{ gNormalHistory, computeReadLayout },
			ResourceData{ gDepthStencilHistory, computeReadLayout }
		)
		.After(atmospherePreCalculateNode, skyboxPreCalculateNode, iblPreCalculateNode, lightingShadowDepthNode, geometryNode, rayTraceGeneralNode)
		.Before(opaqueFence)
		.FrameLocal(ResourceData{ gDepthStencil, computeReadLayout });

	ssrNode->SetRenderPass(P.ssr)
		.Input(
			ResourceData{ gPosition, computeReadLayout }, ResourceData{ gNormal, computeReadLayout }, ResourceData{ gAlbedoOpacity,computeReadLayout },
			ResourceData{ gMetallicRoughness, computeReadLayout }, ResourceData{ atlasShadowMap, computeReadLayout }, ResourceData{ ssaoOutPut, computeReadLayout },
			ResourceData{ gMotionVector, computeReadLayout },
			ResourceData{ hzbMinDepthMap, computeReadLayout }
		)
		// 命中点的漫反射间接光采样 DDGI（可选持久资源：DDGI 从没执行过时为空，不会因此分配）
		.PersistentOption(
			ResourceData{ ddgiIrradianceAtlas, computeReadLayout },
			ResourceData{ ddgiDepthAtlas, computeReadLayout },
			ResourceData{ ddgiProbeData, computeReadLayout }
		)
		.Temp(
			ResourceData{ resbuilder.CreateTexture(ssr_Output, "ssrPass_temp1"), computeWriteLayout },
			ResourceData{ resbuilder.CreateTexture(ssr_Output, "ssrPass_temp2"), computeWriteLayout },
			ResourceData{ resbuilder.CreateTexture(ssr_Output, "ssrPass_temp3"), computeWriteLayout },
			ResourceData{ resbuilder.CreateTexture(ssr_Output, "ssrPass_temp4"), computeWriteLayout },
			ResourceData{ resbuilder.CreateTexture(ssr_Output, "ssrPass_temp5"), computeWriteLayout }		// 空域滤波结果（已去调制），乘回反射率后写输出
		)
		.External(ExternalResourceData{ Ext_RenderTargetColorBuffer, computeReadLayout })
		.Output(ResourceData{ ssr_Output, computeWriteLayout })
		.Persistent(
			ResourceData{ resbuilder.CreateTexture(ssr_Output, "ssrPass_historyColorTexture"), computeReadLayout },
			ResourceData{ resbuilder.CreateTexture(ssr_Output, "ssrPass_historyMomentTexture"), computeReadLayout },
			// 上一帧的 G-Buffer（Persistent 2 ~ 4）
			ResourceData{ gPositionHistory, computeReadLayout },
			ResourceData{ gNormalHistory, computeReadLayout },
			ResourceData{ gDepthStencilHistory, computeReadLayout }
		)
		.After(atmospherePreCalculateNode, skyboxPreCalculateNode, iblPreCalculateNode, geometryNode, lightingNode, ddgiBlendNode)
		.Before(opaqueFence)
		.FrameLocal(ResourceData{ gDepthStencil, computeReadLayout });

	ssgiNode->SetRenderPass(P.ssgi)
		.Input(
			ResourceData{ gPosition, computeReadLayout }, ResourceData{ gNormal, computeReadLayout }, ResourceData{ gAlbedoOpacity,computeReadLayout },
			ResourceData{ gMetallicRoughness, computeReadLayout }, ResourceData{ atlasShadowMap, computeReadLayout }, ResourceData{ ssaoOutPut, computeReadLayout },
			ResourceData{ gMotionVector, computeReadLayout },
			ResourceData{ hzbMinDepthMap, computeReadLayout },
			ResourceData{ ssaoOutPut, computeReadLayout }
		)
		.Temp(
			ResourceData{ resbuilder.CreateTexture(ssgi_Output, "ssgiPass_temp1"), computeWriteLayout },
			ResourceData{ resbuilder.CreateTexture(ssgi_Output, "ssgiPass_temp2"), computeWriteLayout },
			ResourceData{ resbuilder.CreateTexture(ssgi_Output, "ssgiPass_temp3"), computeWriteLayout },
			ResourceData{ resbuilder.CreateTexture(ssgi_Output, "ssgiPass_temp4"), computeWriteLayout },
			ResourceData{ resbuilder.CreateTexture(ssgi_Output, "ssgiPass_temp5"), computeWriteLayout },		// 漫反射空域滤波结果
			// 路径追踪拆成两路：Temp 0 ~ 4 是漫反射，5 ~ 9 是镜面（顺序相同，见 SSGIPass::Execute）
			ResourceData{ resbuilder.CreateTexture(ssgi_Output, "ssgiPass_specTemp1"), computeWriteLayout },
			ResourceData{ resbuilder.CreateTexture(ssgi_Output, "ssgiPass_specTemp2"), computeWriteLayout },
			ResourceData{ resbuilder.CreateTexture(ssgi_Output, "ssgiPass_specTemp3"), computeWriteLayout },
			ResourceData{ resbuilder.CreateTexture(ssgi_Output, "ssgiPass_specTemp4"), computeWriteLayout },
			ResourceData{ resbuilder.CreateTexture(ssgi_Output, "ssgiPass_specTemp5"), computeWriteLayout }
		)
		.External(ExternalResourceData{ Ext_RenderTargetColorBuffer, computeReadLayout })
		.Output(ResourceData{ ssgi_Output, computeWriteLayout })
		.Persistent(
			ResourceData{ resbuilder.CreateTexture(ssgi_Output, "ssgiPass_historyColorTexture"), computeReadLayout },
			ResourceData{ resbuilder.CreateTexture(ssgi_Output, "ssgiPass_historyMomentTexture"), computeReadLayout },
			ResourceData{ resbuilder.CreateTexture(ssgi_Output, "ssgiPass_specHistoryColorTexture"), computeReadLayout },
			ResourceData{ resbuilder.CreateTexture(ssgi_Output, "ssgiPass_specHistoryMomentTexture"), computeReadLayout },
			// 上一帧的 G-Buffer（Persistent 4 ~ 6）
			ResourceData{ gPositionHistory, computeReadLayout },
			ResourceData{ gNormalHistory, computeReadLayout },
			ResourceData{ gDepthStencilHistory, computeReadLayout }
		)
		.After(atmospherePreCalculateNode, skyboxPreCalculateNode, iblPreCalculateNode, geometryNode, lightingNode)
		.Before(opaqueFence)
		.FrameLocal(ResourceData{ gDepthStencil, computeReadLayout });

	// 先清零本帧的重置列表（probeData / probeStats），再光追；多次反弹读的是图集里上一帧的结果
	ddgiUpdateNode->SetRenderPass(P.ddgiUpdate)
		.Output(ResourceData{ ddgiRayData, computeWriteLayout })
		.Persistent(
			ResourceData{ ddgiIrradianceAtlas, computeReadLayout },
			ResourceData{ ddgiDepthAtlas, computeReadLayout },
			ResourceData{ ddgiProbeData, computeWriteLayout },
			ResourceData{ ddgiProbeStats, computeWriteLayout }
		)
		.After(atmospherePreCalculateNode, skyboxPreCalculateNode, iblPreCalculateNode, lightingShadowDepthNode, rayTraceGeneralNode)
		.Before(ddgiBlendNode);

	// 原地更新：只读写更新列表里的探针
	ddgiBlendNode->SetRenderPass(P.ddgiBlend)
		.Input(ResourceData{ ddgiRayData, computeReadLayout })
		.Persistent(
			ResourceData{ ddgiIrradianceAtlas, computeWriteLayout },
			ResourceData{ ddgiDepthAtlas, computeWriteLayout },
			ResourceData{ ddgiProbeData, computeWriteLayout },
			ResourceData{ ddgiProbeStats, computeWriteLayout }
		)
		.After(ddgiUpdateNode)
		.Before(ddgiApplyNode);

	ddgiApplyNode->SetRenderPass(P.ddgiApply)
		.Input(
			ResourceData{ gPosition, computeReadLayout },
			ResourceData{ gNormal, computeReadLayout },
			ResourceData{ gAlbedoOpacity, computeReadLayout },
			ResourceData{ ssaoOutPut, computeReadLayout },
			ResourceData{ gMetallicRoughness, computeReadLayout }
		)
		.Persistent(
			ResourceData{ ddgiIrradianceAtlas, computeReadLayout },
			ResourceData{ ddgiDepthAtlas, computeReadLayout },
			ResourceData{ ddgiProbeData, computeReadLayout }
		)
		.Output(ResourceData{ ddgi_Output, computeWriteLayout })
		.After(geometryNode, ssaoNode, ddgiBlendNode, iblPreCalculateNode)
		.Before(opaqueFence)
		.FrameLocal(ResourceData{ gDepthStencil, computeReadLayout });

	{

		// 漫反射合成
		indirectDiffuseCombinNode->SetRenderPass(P.indirectDiffuseCombin)
			.InputOption(
				ResourceData{ ddgi_Output, computeReadLayout }
			)
			.Output(ResourceData{ indirectDiffuse_Output, computeWriteLayout })
			.After(ddgiApplyNode)
			.Before(opaqueFence);

		// 镜面合成
		indirectSpecularCombinNode->SetRenderPass(P.indirectSpecularCombin)
			.InputOption(
				ResourceData{ rayTraceReflect_Output, computeReadLayout },
				ResourceData{ ssr_Output, computeReadLayout }
			)
			.Output(ResourceData{ indirectSpecular_Output, computeWriteLayout })
			.After(rayTraceReflectNode, ssrNode)
			.Before(opaqueFence);

		// 路径追踪合成：漫反射 + 镜面两个波瓣都有的方案（RTGI + SSGI）
		indirectPathTracingCombinNode->SetRenderPass(P.indirectPathTracingCombin)
			.InputOption(
				ResourceData{ rayTraceGI_Output, computeReadLayout },
				ResourceData{ ssgi_Output, computeReadLayout }
			)
			.Output(ResourceData{ indirectPathTracing_Output, computeWriteLayout })
			.After(rayTraceGINode, ssgiNode)
			.Before(opaqueFence);

		// 最终合成：场景颜色（直接光 + 自发光 + LightingPass 补的 IBL）+ 漫反射 + 镜面 + 路径追踪
		combinIndirectLightingNode->SetRenderPass(P.combinIndirectLighting)
			.Input(
				ResourceData{ indirectDiffuse_Output, computeReadLayout },
				ResourceData{ indirectSpecular_Output, computeReadLayout },
				ResourceData{ indirectPathTracing_Output, computeReadLayout }
			)
			.External(ExternalResourceData{ Ext_RenderTargetColorBuffer, transferReadLayout })
			.Temp(ResourceData{ resbuilder.CreateTexture(sceneColorBuffer, "combinIndirectLightingPass_temp1"), transferWriteLayout })
			.After(indirectDiffuseCombinNode, indirectSpecularCombinNode, indirectPathTracingCombinNode)
			.Before(opaqueFence);
	}

	lightDrawNode->SetRenderPass(P.lightDraw)
		.External(ExternalResourceData{ Ext_RenderTargetColorBuffer, graphicsWriteLayout })
		.After(combinIndirectLightingNode)
		.Before(opaqueFence)
		.FrameLocal(ResourceData{ gDepthStencil, graphicsWriteLayout });

	ddgiProbeDrawNode->SetRenderPass(P.ddgiProbeDraw)
		.Persistent(
			ResourceData{ ddgiIrradianceAtlas, graphicsReadLayout },
			ResourceData{ ddgiDepthAtlas, graphicsReadLayout },
			ResourceData{ ddgiProbeData, graphicsReadLayout }
		)
		.External(ExternalResourceData{ Ext_RenderTargetColorBuffer, graphicsWriteLayout })
		.After(combinIndirectLightingNode, lightDrawNode, ddgiBlendNode)
		.Before(opaqueFence)
		.FrameLocal(ResourceData{ gDepthStencil, graphicsWriteLayout });

	{
		auto volumeResolution = glm::u32vec2(width, height);
		auto scatterColorRes = resbuilder.CreateTexture(volumeResolution, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "");
		auto tramsmittanceRes = resbuilder.CreateTexture(volumeResolution, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "");

		// Lut 和 Combine 之间传递的三张图
		auto volumeScatterColor = resbuilder.CreateTexture(scatterColorRes, "volumeLutNode_ScatterColor");
		auto volumeTramsmittance = resbuilder.CreateTexture(tramsmittanceRes, "volumeLutNode_Tramsmittance");
		auto volumeGaussScatterColor = resbuilder.CreateTexture(scatterColorRes, "volumeLutNode_GaussScatterColor");

		volumeLutNode->SetRenderPass(P.volumeLut)
			.Input(
				ResourceData{ gPosition, computeReadLayout }, ResourceData{ gNormal, computeReadLayout },
				ResourceData{ atlasShadowMap, computeReadLayout },
				ResourceData{ gMotionVector, computeReadLayout }
			)
			.Output(
				ResourceData{ volumeScatterColor, computeWriteLayout },
				ResourceData{ volumeTramsmittance, computeWriteLayout },
				ResourceData{ volumeGaussScatterColor, computeReadLayout }
			)
			.Temp(ResourceData{ resbuilder.CreateTexture(scatterColorRes, "volumeLutNode_TAAScatterColorTexture"), computeReadLayout })
			.Persistent(
				ResourceData{ resbuilder.CreateTexture(scatterColorRes, "volumeLutNode_HistoryScatterColorTexture"), computeReadLayout },
				// 上一帧的 G-Buffer（Persistent 1 ~ 3）
				ResourceData{ gPositionHistory, computeReadLayout },
				ResourceData{ gNormalHistory, computeReadLayout },
				ResourceData{ gDepthStencilHistory, computeReadLayout }
			)
			.FrameLocal(ResourceData{ gDepthStencil, computeReadLayout })
			.After(atmospherePreCalculateNode, skyIblDiffusePreCalculateNode, lightingShadowDepthNode, geometryNode)
			.Before(volumeCombineNode);

		volumeCombineNode->SetRenderPass(P.volumeCombine)
			.Input(
				ResourceData{ volumeScatterColor, computeReadLayout },
				ResourceData{ volumeTramsmittance, computeReadLayout },
				ResourceData{ volumeGaussScatterColor, computeReadLayout }
			)
			.External(ExternalResourceData{ Ext_RenderTargetColorBuffer, computeWriteLayout })
			.Temp(ResourceData{ resbuilder.CreateTexture(sceneColorBuffer, "volumeCombineNode_TempSceneColor"), computeWriteLayout })
			.FrameLocal(ResourceData{ gDepthStencil, computeReadLayout })
			.After(opaqueFence, volumeLutNode)
			.Before(transprantFence);
	}

	// 把本帧的 gPosition / gNormal / gDepthStencil 拷进历史
	gBufferHistoryCopyNode->SetRenderPass(P.gBufferHistoryCopy)
		.Input(ResourceData{ gPosition, transferReadLayout }, ResourceData{ gNormal, transferReadLayout })
		.Persistent(
			ResourceData{ gPositionHistory, transferWriteLayout },
			ResourceData{ gNormalHistory, transferWriteLayout },
			ResourceData{ gDepthStencilHistory, transferWriteLayout }
		)
		.After(opaqueFence, volumeLutNode)
		.FrameLocal(ResourceData{ gDepthStencil, transferReadLayout });

	transparentNode->SetRenderPass(P.transparent)
		.Input(ResourceData{ atlasShadowMap, graphicsReadLayout })
		.External(ExternalResourceData{ Ext_RenderTargetColorBuffer, graphicsWriteLayout })
		.After(opaqueFence, volumeCombineNode)
		.Before(effectNode, transprantFence)
		.Temp(
			ResourceData{ resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "transparentNode_oitAccum"), graphicsWriteLayout },
			ResourceData{ resbuilder.CreateTexture(width, height, vk::Format::eR16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "transparentNode_oitReveal"), graphicsWriteLayout }
		)
		.FrameLocal(ResourceData{ gDepthStencil, graphicsWriteLayout });

	effectNode->SetRenderPass(P.effect)
		.External(ExternalResourceData{ Ext_RenderTargetColorBuffer, graphicsWriteLayout })
		.After(opaqueFence, volumeCombineNode, transparentNode)
		.Before(transprantFence)
		.Temp(
			ResourceData{ resbuilder.CreateTexture(width, height, vk::Format::eR16G16B16A16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "effectNode_oitAccum"), graphicsWriteLayout },
			ResourceData{ resbuilder.CreateTexture(width, height, vk::Format::eR16Sfloat, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge, "effectNode_oitReveal"), graphicsWriteLayout }
		)
		.FrameLocal(ResourceData{ gDepthStencil, graphicsWriteLayout });

	autoExposureNode->SetRenderPass(P.autoExposure)
		.After(postProcessFence)
		.External(ExternalResourceData{ Ext_RenderTargetColorBuffer, computeReadLayout });

	// 编辑器拾取：几何之后读实体 ID 缓冲和 gPosition。只有编辑器提交了请求的那一帧真正执行（见 EditorPickPass）。
	// 两张图都按 computeReadLayout 读：gPosition 同时有 SSAO / 光照等读者，渲染图不给同一资源的多个读者排序，布局必须一致
	RenderGraph::PassNode* editorPickNode = nullptr;
	if (gEntityId && P.editorPick)
	{
		editorPickNode = _sceneRenderGraph->AddNode("editorPickNode");
		editorPickNode->SetRenderPass(P.editorPick)
			.Input(ResourceData{ *gEntityId, computeReadLayout })
			.Input(ResourceData{ gPosition, computeReadLayout })
			.After(geometryNode);
	}

	// 编辑器选中遮罩：几何（含蒙皮）之后，读实体 ID 缓冲判断可见性。排在拾取之后：两者都要切实体 ID 缓冲的布局，不能并行
	if (gEntityId && P.editorSelectionMask)
	{
		auto Ext_EditorSelectionMask = _sceneRenderGraph->CreateExternalTexture(Ext_EditorSelectionMask_Name);
		auto editorSelectionMaskNode = _sceneRenderGraph->AddNode("editorSelectionMaskNode");
		editorSelectionMaskNode->SetRenderPass(P.editorSelectionMask)
			.Input(ResourceData{ *gEntityId, graphicsReadLayout })
			.External(ExternalResourceData{ Ext_EditorSelectionMask, graphicsWriteLayout })
			.After(geometryNode);
		if (editorPickNode)
			editorSelectionMaskNode->After(editorPickNode);
	}

	_sceneRenderGraph->Compile();

	if (oldGraph)
	{
		const uint32_t inherited = _sceneRenderGraph->InheritPersistentResources(*oldGraph);
		oldGraph.reset();
		std::cout << std::format("[ Renderer ] 渲染图按 {}x{} 重建，沿用 {} 个尺寸无关的持久资源\n", width, height, inherited);
	}
	// 尺寸相关的资源（含时域历史）都是新分配的：通知各 Pass 清历史
	if (rebuild)
		P.ForEach([&](RenderPassBase& pass) { pass.OnResize(width, height); });
}

void VKRenderer3D::ClearColorTexture(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, const std::shared_ptr<Texture2D>& texture, const vk::ClearColorValue& clearColor)
{
	if (!texture || texture->IsEmpty())
		return;
	auto image = texture->GetImage();
	auto layout = texture->GetLayout(0);

	if (layout != vk::ImageLayout::eTransferDstOptimal && layout != vk::ImageLayout::eGeneral)
		texture->TransitionLayout(cmd, &layout, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	vk::ImageSubresourceRange range;
	range.setAspectMask(vk::ImageAspectFlagBits::eColor)
		.setBaseArrayLayer(0)
		.setLayerCount(1)
		.setBaseMipLevel(0)
		.setLevelCount(1);
	cmd->clearColorImage(image, layout, clearColor, range);
}

void VKRenderer3D::OnDispatch(FrameJob& job, const FrameJob* prev, uint32_t stageIndex)
{
	auto data = std::make_shared<Frame3DData>();
	auto& state = *job.state;

	state.option = _option;
	data->renderTarget = _renderTargets[job.flightIndex % _renderTargets.size()];

	state.curFrameSkinnedData = std::make_shared<FrameHistoryData>();
	state.preFrameSkinnedData = prev ? prev->state->curFrameSkinnedData : nullptr;

	data->skipScene = !_sceneRenderEnabled.load(std::memory_order_acquire);

	// 相机历史只跟真正画过场景的帧走，3D 关闭的帧不参与
	if (!data->skipScene)
		ResolveFrameHistory(state, *data);

	// 编辑器选中：这一帧用的快照（遮罩 Pass 和描边 Pass 读同一份）。没选东西时为空，两个 Pass 都跳过
	state.editorSelection.reset();
	if (GlobalConfig::EditorViewportFeatures && !data->skipScene)
	{
		auto selection = _editorViewportChannel->GetSelection();
		if (selection && !selection->Empty())
		{
			auto frameSelection = std::make_shared<EditorFrameSelection>();
			frameSelection->selection = std::move(selection);
			frameSelection->style = _editorViewportChannel->GetStyle();
			state.editorSelection = std::move(frameSelection);
		}
	}

	job.stageData[stageIndex] = std::move(data);
}

// 每帧固定 8 段，顺序是：
//   1 SetupRenderState   2 SetupIndirectDrawData   3 PreCalculate   4 Skinned   5 预计算 + 蒙皮的提交
//   6 清中间目标          7 SceneRenderGraph        8 后处理（Combin / Bloom / PostProcess → 最终画面）+ 收尾
// 3D 关闭时各段照样进出（空段），只在第 8 段把最终画面清黑。
// 要加段、减段，或者让某一段按条件跳过，都会让前后帧的段对不上号（见 VKFramePipeline.h）。
void VKRenderer3D::Execute(FrameJob& job, uint32_t stageIndex)
{
	auto* data = job.GetStageData<Frame3DData>(stageIndex);
	auto& state = job.state;
	if (!data || !state || !job.sync)
		return;

	auto& sync = *job.sync;
	RenderFrameProfile* profile = job.Profile();
	const bool skip = data->skipScene;
	auto& renderTarget = data->renderTarget;

	{
		FlightSync::ProgressScope syncScope(sync);
		RenderProfileScope scope(profile, RenderProfileStep::SetupRenderState);
		if (!skip)
			SetupRenderState(*state, *data);
	}

	{
		FlightSync::ProgressScope syncScope(sync);
		RenderProfileScope scope(profile, RenderProfileStep::SetupIndirectDrawData);
		if (!skip)
			SetupIndirectDrawData(state);
	}

	{
		std::shared_ptr<VKWrapper::VKCommandBuffer> cmd;
		if (!skip)
			cmd = VKCONTEXT->GetCommandBuffer();

		{
			FlightSync::ProgressScope syncScope(sync);
			RenderProfileScope scope(profile, RenderProfileStep::PreCalculate);
			if (!skip)
				_preCalculatePass->Execute(cmd, *state);
		}

		{
			FlightSync::ProgressScope syncScope(sync);
			RenderProfileScope scope(profile, RenderProfileStep::Skinned);
			if (!skip)
				_skinnedPass->Execute(cmd, *state);
		}

		{
			FlightSync::ProgressScope syncScope(sync);
			RenderProfileScope scope(profile, RenderProfileStep::PreSubmit);
			if (!skip)
				cmd->SubmitNowAndWait();
		}
	}

	{
		FlightSync::ProgressScope syncScope(sync);
		RenderProfileScope scope(profile, RenderProfileStep::ClearTargets);
		if (!skip)
		{
			auto cmd = VKCONTEXT->GetCommandBuffer();
			ClearColorTexture(cmd, renderTarget->sceneColorBuffer, vk::ClearColorValue{ 0.0f, 0.0f, 0.0f, 1.0f });
			ClearColorTexture(cmd, renderTarget->firstPersonColorBuffer, vk::ClearColorValue{ 0.0f, 0.0f, 0.0f, 0.0f });
			cmd->SubmitNowAndWait();
		}
	}

	{
		FlightSync::ProgressScope syncScope(sync);
		RenderProfileScope scope(profile, RenderProfileStep::SceneGraph);
		if (!skip)
		{
			std::unordered_map<std::string, std::shared_ptr<Texture2D>> externalResources = {
				{ Ext_RenderTargetColorBuffer_Name, renderTarget->sceneColorBuffer }
			};
			if (renderTarget->editorSelectionMask)
				externalResources.emplace(Ext_EditorSelectionMask_Name, renderTarget->editorSelectionMask);
			_sceneRenderGraph->Execute(state, externalResources);
		}
	}

	// 后处理：Combin / Bloom / PostProcess 这几个 Pass 各帧共用，放进同一段，同一时间只有一帧在用。
	// Finish 也并在这一段里：只记一下 EV100
	{
		FlightSync::ProgressScope syncScope(sync);
		if (skip)
		{
			auto cmd = VKCONTEXT->GetCommandBuffer();
			ClearColorTexture(cmd, job.finalColor, vk::ClearColorValue{ 0.0f, 0.0f, 0.0f, 1.0f });
			cmd->SubmitNowAndWait();
		}
		else
		{
			{
				RenderProfileScope scope(profile, RenderProfileStep::Combin);
				_combinPass->Draw(renderTarget->combinColorBuffer, renderTarget->combinBrightColorBuffer, { renderTarget->sceneColorBuffer ,renderTarget->firstPersonColorBuffer });
			}
			auto& option = state->option;
			if (option.flags.bloomOn)
			{
				RenderProfileScope scope(profile, RenderProfileStep::Bloom);
				_globalBloomPass->Draw(renderTarget->combinBrightColorBuffer, renderTarget->bloomMipBuffers);
			}
			{
				RenderProfileScope scope(profile, RenderProfileStep::PostProcess);
				_globalPostProcessPass->Draw(
					job.finalColor, renderTarget->combinColorBuffer, renderTarget->bloomMipBuffers[0],
					option.flags.bloomOn, option.flags.gammaOn, false,
					pow(2.0f, option.postProcessParams.EV100), option.postProcessParams.gamma
				);

				// 编辑器选中描边：画在最终画面上（2D 阶段在后面，2D 精灵会盖在 3D 描边上面）
				if (_editorOutlinePass && state->editorSelection && renderTarget->editorSelectionMask)
					_editorOutlinePass->Draw(job.finalColor, renderTarget->editorSelectionMask, *state->editorSelection);
			}
		}

		RenderProfileScope scope(profile, RenderProfileStep::Finish);
		_record.prevEV100 = state->option.postProcessParams.EV100;
	}
}

camera_compData VKRenderer3D::BuildCameraCompData(const RenderState& state) const
{
	const auto& camera = state.camera;

	camera_compData cameraData{};
	cameraData.projection = camera.projection;
	cameraData.view = camera.view;
	cameraData.projView = cameraData.projection * cameraData.view;
	cameraData.invProjection = glm::inverse(camera.projection);
	cameraData.invView = glm::inverse(camera.view);
	cameraData.invProjView = glm::inverse(cameraData.projView);
	glm::mat4 invTrans = glm::mat3(glm::transpose(cameraData.invView));
	cameraData.invTransViewRow1 = invTrans[0];
	cameraData.invTransViewRow2 = invTrans[1];
	cameraData.invTransViewRow3 = invTrans[2];
	cameraData.position = camera.position;
	cameraData.direction = camera.direction;
	cameraData.directionUp = camera.directionUp;
	cameraData.directionRight = camera.directionRight;
	cameraData.nearPlane = camera.nearPlane;
	cameraData.farPlane = camera.farPlane;
	cameraData.fov = camera.fov;
	return cameraData;
}

void VKRenderer3D::ResolveFrameHistory(RenderState& state, Frame3DData& data)
{
	// 相机：prev 取上一张真正画过场景的帧，cur 取本帧
	data.cameraCurData = BuildCameraCompData(state);
	data.cameraPrevData = _cameraCacheValid ? _cameraCache : data.cameraCurData;
	_cameraCache = data.cameraCurData;
	_cameraCacheValid = true;

	// 蒙皮重投影历史在构建 FrameData 时已经挂好（cur/preFrameSkinnedData），这里不用再管。

	auto& staticRenderData = state.objects.sceneStaticRenderData;
	auto& skinnedRenderData = state.objects.sceneSkinnedRenderData;
	const uint32_t frameIndex = state.renderRecord.frameIndex;

	// 同一物体可能被拆成多个 item（不透明/透明、多个 mesh），它们共享同一份 history，
	// 所以必须先把所有 item 的 prev 都解析完，再统一推进，否则后面的 item 会读到刚被推进的值。
	auto resolve = [](auto& items)
		{
			if (items.empty())
				return;

			std::for_each(
				std::execution::par_unseq,
				items.begin(),
				items.end(),
				[&](auto& item) {
					if (item.transformHistory)
					{
						const auto& history = *item.transformHistory;
						item.prevTransform = history.valid ? history.prevTransform : item.transform;// 第一次出现（或物体被剔除过几帧）时没有可用的 prev，退化成零运动
					}
				});
		};

	auto commit = [frameIndex](auto& items)
		{
			if (items.empty())
				return;

			for (auto& item : items)
			{
				if (item.transformHistory)
				{
					auto& history = *item.transformHistory;
					if (history.committedFrame != frameIndex) // 同一物体的多个 item 拿到的是同一个 transform，一帧推进一次就够
					{
						history.prevTransform = item.transform;
						history.valid = true;
						history.committedFrame = frameIndex;
					}
				}
			}
		};

	resolve(staticRenderData.opaqueItems);
	resolve(staticRenderData.transparentItems);
	resolve(skinnedRenderData.opaqueOneSideItems);
	resolve(skinnedRenderData.opaqueTwoSideItems);
	resolve(skinnedRenderData.transparentItems);

	commit(staticRenderData.opaqueItems);
	commit(staticRenderData.transparentItems);
	commit(skinnedRenderData.opaqueOneSideItems);
	commit(skinnedRenderData.opaqueTwoSideItems);
	commit(skinnedRenderData.transparentItems);
}

void VKRenderer3D::SetupRenderState(RenderState& state, const Frame3DData& data)
{
	auto cmd = VKCONTEXT->GetCommandBuffer();

	state.camera.prevUBO = std::make_shared<UniformBlock>(sizeof(camera_compData));
	state.camera.curUBO = std::make_shared<UniformBlock>(sizeof(camera_compData));

	state.camera.prevUBO->WriteDataAsync(cmd, &data.cameraPrevData, sizeof(camera_compData));
	state.camera.curUBO->WriteDataAsync(cmd, &data.cameraCurData, sizeof(camera_compData));
	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

	state.framebuffer.width = scr_width;
	state.framebuffer.height = scr_height;

	state.renderRecord.prevEV100 = _record.prevEV100;
}

void VKRenderer3D::SetupIndirectDrawData(std::shared_ptr<RenderState>& state)
{
	std::unordered_set<Material*> willUpdateMaterials;
	std::unordered_set<Mesh*> willUpdateMeshs;
	std::unordered_set<SkinnedMesh*> willUpdateSkinneds;

	auto indirectManager = IndirectDrawManager::Instance();

	BindlessTextureManager::Instance()->RefreshChangedTextures();

	{
		auto updateMesh = [&](auto& items)
			{
				for (auto& item : items)
				{
					auto& mesh = item.meshinfo.mesh;
					auto& material = item.meshinfo.material;

					if (material->GetNeedUpdateIndirectDraw())
						willUpdateMaterials.insert(material.get());
					if (mesh->GetNeedUpdateIndirectDraw())
					{
						willUpdateMeshs.insert(mesh.get());

						using MeshType = std::remove_cv_t<std::remove_reference_t<decltype(*mesh)>>;
						if constexpr (std::is_same_v<MeshType, SkinnedMesh>)
							willUpdateSkinneds.insert(mesh.get());
					}
				}
			};

		updateMesh(state->objects.sceneStaticRenderData.opaqueItems);
		updateMesh(state->objects.sceneStaticRenderData.transparentItems);
		updateMesh(state->objects.sceneSkinnedRenderData.opaqueOneSideItems);
		updateMesh(state->objects.sceneSkinnedRenderData.opaqueTwoSideItems);
		updateMesh(state->objects.sceneSkinnedRenderData.transparentItems);

		// 粒子用到的材质 / 自定义模型：按批次登记（一个批次内渲染参数相同，不用逐粒子看）
		auto updateParticleRenderInfo = [&](const ParticleRenderInfo& renderInfo)
			{
				auto& material = renderInfo.material;
				if (material && material->GetNeedUpdateIndirectDraw())
					willUpdateMaterials.insert(material.get());

				if (renderInfo.mesh && renderInfo.model)
				{
					for (auto& info : renderInfo.model->GetMeshInfos())
					{
						if (info.mesh && info.mesh->GetNeedUpdateIndirectDraw())
							willUpdateMeshs.insert(info.mesh.get());

						if (info.material && info.material->GetNeedUpdateIndirectDraw())
							willUpdateMaterials.insert(info.material.get());
					}
				}
			};

		for (auto& frame : state->objects.sceneStaticRenderData.particleFrames)
		{
			if (!frame)
				continue;
			for (auto& batch : frame->batches)
				updateParticleRenderInfo(batch.renderInfo);
		}
	}

	if (!willUpdateMeshs.empty())
	{
		indirectManager->WithWriteLock(
			{ IndirectDrawManager::SharedLockType::Mesh },
			[&]()
			{
				std::for_each(std::execution::seq, willUpdateMeshs.begin(), willUpdateMeshs.end(),
					[&](auto& meshptr) {
						indirectManager->SetupMesh_LockFree(*meshptr);
						meshptr->SetNeedUpdateIndirectDraw(false);
					});
			});
	}


	if (!willUpdateMaterials.empty())
	{
		indirectManager->WithWriteLock(
			{ IndirectDrawManager::SharedLockType::Material },
			[&]()
			{
				std::for_each(std::execution::seq, willUpdateMaterials.begin(), willUpdateMaterials.end(),
					[&](auto& materialptr) {
						materialptr->SetNeedUpdateIndirectDraw(false);
						indirectManager->SetupMaterial_LockFree(*materialptr);
					});
			});
	}

	if (!willUpdateSkinneds.empty())
	{
		indirectManager->WithWriteLock(
			{ IndirectDrawManager::SharedLockType::Skinned },
			[&]()
			{
				std::for_each(std::execution::seq, willUpdateSkinneds.begin(), willUpdateSkinneds.end(),
					[&](SkinnedMesh* mesh) {
						indirectManager->SetupSkinned_LockFree(*mesh);
					});
			});
	}
}

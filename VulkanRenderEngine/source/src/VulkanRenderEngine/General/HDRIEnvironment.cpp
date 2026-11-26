#include "vkstdafx.h"
#include "VulkanRenderEngine/General/HDRIEnvironment.h"
#include "VulkanRenderEngine/General/IBLImageBuilder.h"
#include "VulkanRenderEngine/Base/ComputePipeline.h"
#include "VulkanRenderEngine/VKContext.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb/stb_image.h>

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <vector>

namespace
{
	constexpr const char* kEquirectToCubeShader = "shader/IBL/equirectToCube.comp";
	constexpr vk::Format kCubeFormat = vk::Format::eR16G16B16A16Sfloat;
	constexpr uint32_t kBRDFLUTSize = 512;
	constexpr uint32_t kBRDFLUTSamples = 1024;
	constexpr uint32_t kSampleSeed = 1;			// 固定种子：同一张图每次生成的结果一样
	constexpr float kMaxHalfValue = 60000.0f;	// 半精度最大 65504，太阳之类的超亮像素截在这以内，不变成 inf

	// 和 equirectToCube.comp 的 push_constant 一致
	struct EquirectToCubeParams
	{
		uint32_t samplesPerAxis;	// 每个输出像素在每个方向上取几个子样本（盒式滤波，输出比源图小时防走样）
		float maxValue;
		uint32_t pad0;
		uint32_t pad1;
	};

	// 所有环境共用：转换管线、IBLImageBuilder、BRDF LUT。生成过程整段持锁（IBLImageBuilder / 管线的绑定记录不是线程安全的）
	struct SharedResources
	{
		std::unique_ptr<ComputePipeline> equirectToCube;
		std::unique_ptr<IBLImageBuilder> iblBuilder;
		std::shared_ptr<Texture2D> brdfLUT;
	};

	std::mutex g_mutex;
	std::unique_ptr<SharedResources> g_shared;
	bool g_released = false;
	bool g_shutdownRegistered = false;

	void SetError(std::string* error, std::string message)
	{
		std::cerr << "[ HDRIEnvironment ] " << message << "\n";
		if (error)
			*error = std::move(message);
	}

	// 调用方持有 g_mutex
	SharedResources* AcquireSharedLocked(std::string* error)
	{
		if (g_released)
		{
			SetError(error, "设备已经销毁");
			return nullptr;
		}
		if (g_shared)
			return g_shared.get();

		if (!g_shutdownRegistered)
		{
			VKCONTEXT->AddShutdownCallback([] { HDRIEnvironment::ReleaseShared(); });
			g_shutdownRegistered = true;
		}

		auto shared = std::make_unique<SharedResources>();
		{
			ComputePipelineConfig config;
			config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
			config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
			config.computePath = kEquirectToCubeShader;
			config
				.AddStorageImage2DArray(0)
				.AddUnifromTexture(1);
			config.AddPushConstant(sizeof(EquirectToCubeParams));

			shared->equirectToCube = std::make_unique<ComputePipeline>();
			if (!config.Validate() || !shared->equirectToCube->Create(config) || !shared->equirectToCube->IsValid())
			{
				SetError(error, std::format("创建 {} 管线失败", kEquirectToCubeShader));
				return nullptr;
			}
		}
		shared->iblBuilder = std::make_unique<IBLImageBuilder>();
		g_shared = std::move(shared);
		return g_shared.get();
	}

	bool ReadFileBytes(const std::string& path, std::vector<unsigned char>& bytes)
	{
		std::ifstream file(std::filesystem::path(path), std::ios::binary | std::ios::ate);
		if (!file)
			return false;
		const std::streamsize size = file.tellg();
		if (size <= 0)
			return false;
		bytes.resize(static_cast<size_t>(size));
		file.seekg(0);
		return static_cast<bool>(file.read(reinterpret_cast<char*>(bytes.data()), size));
	}

	uint32_t FullMipCount(uint32_t size)
	{
		return static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(std::max(size, 1u))))) + 1;
	}

	// 输出 cube 每个像素覆盖 90° / size，经纬图每个像素 360° / width：子样本数 ≈ 两者之比，至少 2×2（边缘抗锯齿）
	uint32_t SamplesPerAxis(uint32_t equirectWidth, uint32_t cubeSize)
	{
		const float ratio = static_cast<float>(equirectWidth) / 4.0f / static_cast<float>(std::max(cubeSize, 1u));
		return std::clamp(static_cast<uint32_t>(std::ceil(ratio)), 2u, 8u);
	}

	void DispatchEquirectToCube(SharedResources& shared, const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		const std::shared_ptr<Texture2D>& equirect, const std::shared_ptr<TextureCube>& cube)
	{
		ComputeBindingRecord binding;
		binding.SetStorageImage2DArrayLevel(cube, vk::ImageAspectFlagBits::eColor, 0, 1, 0);
		binding.SetUniformTexture(equirect, vk::ImageAspectFlagBits::eColor, 1);
		shared.equirectToCube->Bind(cmd, binding);

		const EquirectToCubeParams params{
			.samplesPerAxis = SamplesPerAxis(equirect->GetWidth(), cube->GetWidth()),
			.maxValue = kMaxHalfValue,
			.pad0 = 0,
			.pad1 = 0,
		};
		shared.equirectToCube->SetPushConstants(cmd, &params, sizeof(params));

		const uint32_t size = cube->GetWidth();
		cmd->dispatch(
			(size + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X,
			(size + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y,
			6);
	}
}

void HDRIEnvironment::ReleaseShared()
{
	std::lock_guard lock(g_mutex);
	g_shared.reset();
	g_released = true;
}

std::shared_ptr<HDRIEnvironment> HDRIEnvironment::LoadFromFile(const std::string& path, std::string* error, const HDRIEnvironmentSettings& settings)
{
	// ---------------- 读文件、解码（不占锁） ----------------
	std::vector<unsigned char> bytes;
	if (!ReadFileBytes(path, bytes))
	{
		SetError(error, std::format("读不了文件：{}", path));
		return nullptr;
	}

	int width = 0;
	int height = 0;
	int components = 0;
	// .hdr（Radiance RGBE）读出来就是线性值；普通 8 位图片 stb 会按 gamma 2.2 转成线性
	float* pixels = stbi_loadf_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &components, 4);
	bytes.clear();
	bytes.shrink_to_fit();
	if (!pixels || width <= 0 || height <= 0)
	{
		const char* reason = stbi_failure_reason();
		SetError(error, std::format("解码失败：{}（{}）", path, reason ? reason : "未知原因"));
		if (pixels)
			stbi_image_free(pixels);
		return nullptr;
	}
	if (width != height * 2)
		std::cerr << std::format("[ HDRIEnvironment ] {} 是 {}×{}，不是 2:1 的经纬图，结果可能变形\n", path, width, height);

	// 半精度：RGBA16F 一定支持线性过滤（RGBA32F 的线性过滤是可选特性），体积也小一半
	const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
	std::vector<uint16_t> halfPixels(pixelCount * 4);
	for (size_t i = 0; i < pixelCount; ++i)
	{
		for (size_t c = 0; c < 3; ++c)
		{
			float value = pixels[i * 4 + c];
			value = std::isfinite(value) ? std::clamp(value, 0.0f, kMaxHalfValue) : 0.0f;
			halfPixels[i * 4 + c] = glm::packHalf1x16(value);
		}
		halfPixels[i * 4 + 3] = glm::packHalf1x16(1.0f);
	}
	stbi_image_free(pixels);

	// ---------------- GPU 生成（整段持锁） ----------------
	std::lock_guard lock(g_mutex);
	SharedResources* shared = AcquireSharedLocked(error);
	if (!shared)
		return nullptr;

	TextureConfig equirectConfig;
	equirectConfig.minFilter = vk::Filter::eLinear;
	equirectConfig.magFilter = vk::Filter::eLinear;
	equirectConfig.wrapU = vk::SamplerAddressMode::eRepeat;			// 经度方向首尾相接
	equirectConfig.wrapV = vk::SamplerAddressMode::eClampToEdge;
	auto equirect = std::make_shared<Texture2D>(static_cast<uint32_t>(width), static_cast<uint32_t>(height), kCubeFormat, equirectConfig, 1, true);
	if (equirect->IsEmpty())
	{
		SetError(error, std::format("创建 {}×{} 的贴图失败", width, height));
		return nullptr;
	}
	equirect->UpdateTextureData(halfPixels.data());
	halfPixels.clear();
	halfPixels.shrink_to_fit();

	const TextureConfig cubeConfig = TextureConfig::GetDefaultSkyCubeConfig();
	const uint32_t backgroundSize = std::max(settings.backgroundSize, 16u);
	const uint32_t sourceSize = std::max(settings.sourceSize, 16u);
	const uint32_t diffuseSize = std::max(settings.diffuseSize, 4u);
	const uint32_t prefilterSize = std::max(settings.prefilterSize, 8u);
	const uint32_t prefilterLevels = std::clamp(settings.prefilterLevels, 2u, FullMipCount(prefilterSize));

	auto background = std::make_shared<TextureCube>(backgroundSize, kCubeFormat, cubeConfig, FullMipCount(backgroundSize));
	auto source = std::make_shared<TextureCube>(sourceSize, kCubeFormat, cubeConfig, 1);
	auto diffuse = std::make_shared<TextureCube>(diffuseSize, kCubeFormat, cubeConfig, 1);
	auto prefilter = std::make_shared<TextureCube>(prefilterSize, kCubeFormat, cubeConfig, prefilterLevels);
	if (background->IsEmpty() || source->IsEmpty() || diffuse->IsEmpty() || prefilter->IsEmpty())
	{
		SetError(error, "创建 cube 贴图失败");
		return nullptr;
	}

	// 1) 经纬图 → 背景 cube（第 0 级）和源 cube
	{
		auto cmd = VKCONTEXT->GetCommandBuffer();
		if (!cmd)
		{
			SetError(error, "拿不到命令缓冲");
			return nullptr;
		}
		DispatchEquirectToCube(*shared, cmd, equirect, background);
		DispatchEquirectToCube(*shared, cmd, equirect, source);
		if (cmd->IsRecording())
			cmd->SubmitNowAndWait();
	}
	equirect.reset();

	// 2) 背景 cube 的 mip（背景模糊用），自己提交
	background->GenerateTextureMipMaps();

	// 3) IBLImageBuilder：漫反射、预过滤、BRDF LUT（LUT 和环境无关，只生成一次）
	{
		auto cmd = VKCONTEXT->GetCommandBuffer();
		if (!cmd)
		{
			SetError(error, "拿不到命令缓冲");
			return nullptr;
		}

		IBLImageBuilder::DiffuseGenerateParams diffuseParams{ .samplerCount = settings.diffuseSamples, .frameIndex = kSampleSeed };
		shared->iblBuilder->CalculateCubeDiffuse(cmd, diffuse, source, diffuseParams);

		IBLImageBuilder::PrefilterGenerateParams prefilterParams{ .samplerCount = settings.prefilterSamples, .frameIndex = kSampleSeed };
		shared->iblBuilder->CalculateCubePrefilter(cmd, prefilter, source, prefilterParams);

		if (!shared->brdfLUT)
		{
			auto lut = std::make_shared<Texture2D>(kBRDFLUTSize, kBRDFLUTSize, kCubeFormat, TextureConfig::GetDefaultSkyCubeConfig());
			if (!lut->IsEmpty())
			{
				IBLImageBuilder::BRDFLUTGenerateParams lutParams{ .samplerCount = kBRDFLUTSamples, .frameIndex = kSampleSeed };
				shared->iblBuilder->CalculateBRDFLUT(cmd, lut, lutParams);
				shared->brdfLUT = lut;
			}
		}

		if (cmd->IsRecording())
			cmd->SubmitNowAndWait();
	}
	source.reset();

	if (!shared->brdfLUT)
	{
		SetError(error, "生成 BRDF LUT 失败");
		return nullptr;
	}

	// 4) 全部切成只读采样布局：之后预览在哪个线程、哪一帧采样都不用再做布局转换
	background->MakeSampledImmutable();
	diffuse->MakeSampledImmutable();
	prefilter->MakeSampledImmutable();
	shared->brdfLUT->MakeSampledImmutable();

	auto environment = std::make_shared<HDRIEnvironment>();
	environment->_path = path;
	environment->_sourceWidth = static_cast<uint32_t>(width);
	environment->_sourceHeight = static_cast<uint32_t>(height);
	environment->_background = std::move(background);
	environment->_diffuse = std::move(diffuse);
	environment->_prefilter = std::move(prefilter);
	environment->_brdfLUT = shared->brdfLUT;
	return environment;
}

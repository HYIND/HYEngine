#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/Texture2D.h"
#include "VulkanRenderEngine/Base/TextureCube.h"

#include <memory>
#include <string>

// HDRI 环境的生成参数（边长都是 cube 每个面的像素数）
struct HDRIEnvironmentSettings
{
	uint32_t backgroundSize = 512;		// 背景 cube：直接显示用，带完整 mip（背景模糊按 mip 取）
	uint32_t sourceSize = 128;			// 交给 IBLImageBuilder 的源 cube：IBLImageBuilder 只采样第 0 级，
										// 源图分辨率低一点（相当于先做了一次盒式模糊）随机采样的噪点小很多
	uint32_t diffuseSize = 32;			// 漫反射辐照度 cube（本来就非常平滑）
	uint32_t prefilterSize = 128;		// 镜面预过滤 cube 第 0 级的边长
	uint32_t prefilterLevels = 6;		// 预过滤 mip 级数：第 i 级粗糙度 = i / (级数 - 1)，和场景的取法一致
	uint32_t diffuseSamples = 2048;
	uint32_t prefilterSamples = 1024;
};

// 从等距柱状投影（equirectangular / 经纬图）的 .hdr 文件生成一套 IBL：
//   .hdr（stb_image 读成线性 float，转半精度上传）
//     → 计算着色器 shader/IBL/equirectToCube.comp 转成 TextureCube（背景 cube + 小一号的源 cube，都按超采样取平均）
//     → IBLImageBuilder：漫反射辐照度 cube、镜面预过滤 cube（按粗糙度分 mip）、BRDF LUT（所有环境共用一张）
// 约定和场景 IBL（SkyBoxPreCalculatePass / lightingHelper.comp 的 CalculateIBLLightingSplit）一致：
//   漫反射 = albedo / π × (1 - metallic) × texture(diffuse, N)；镜面 = textureLod(prefilter, R, roughness × (级数 - 1)) × (F0 × A + B)。
// 方向约定：Y 轴朝上，经纬图正中间（u = 0.5）对着 -Z，u 增大往 +X 转（右手系里从 -Z 往右转）。
//
// 生成完所有贴图都切成只读采样布局（MakeSampledImmutable），之后可以在任意线程、任意帧同时采样。
// LoadFromFile 是同步的（读文件 + 若干次 GPU 提交并等待），可以在后台线程调用；多个线程同时调用时内部排队。
// 所有对象都要在设备销毁（VKContext shutdown）之前释放。
class HDRIEnvironment
{
public:
	static std::shared_ptr<HDRIEnvironment> LoadFromFile(const std::string& path, std::string* error = nullptr,
		const HDRIEnvironmentSettings& settings = {});

	// 设备销毁前释放共用的管线和 BRDF LUT（第一次 LoadFromFile 时已经自动登记到 VKContext 的 shutdown 回调）
	static void ReleaseShared();

	const std::string& GetPath() const { return _path; }
	uint32_t GetSourceWidth() const { return _sourceWidth; }
	uint32_t GetSourceHeight() const { return _sourceHeight; }

	const std::shared_ptr<TextureCube>& GetBackground() const { return _background; }
	const std::shared_ptr<TextureCube>& GetDiffuse() const { return _diffuse; }
	const std::shared_ptr<TextureCube>& GetPrefilter() const { return _prefilter; }
	const std::shared_ptr<Texture2D>& GetBRDFLUT() const { return _brdfLUT; }

private:
	std::string _path;
	uint32_t _sourceWidth = 0;
	uint32_t _sourceHeight = 0;

	std::shared_ptr<TextureCube> _background;
	std::shared_ptr<TextureCube> _diffuse;
	std::shared_ptr<TextureCube> _prefilter;
	std::shared_ptr<Texture2D> _brdfLUT;
};

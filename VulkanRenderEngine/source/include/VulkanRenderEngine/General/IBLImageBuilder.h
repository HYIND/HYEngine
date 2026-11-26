#pragma once

#include "VulkanRenderEngine/Base/ComputePipeline.h"


// IBL 预计算：漫反射辐照度 cube、镜面预过滤 cube、BRDF LUT（split-sum）。
//   全部是确定性的（不用随机数、不随帧变化），同样的输入每次结果一样：
//   - 漫反射：在源 cube 缩小到每面 gridSize² 的那级 mip 上精确求和（不是随机采样）；
//   - 预过滤：Hammersley + GGX 重要性采样，每个采样按立体角从源 cube 的对应 mip 取值（filtered importance sampling），
//     按 Σ NdotL 归一化；第 i 级粗糙度 = i / (级数 - 1)，和 lightingHelper.comp 的取法 roughness × (级数 - 1) 对应；
//   - BRDF LUT：Hammersley，几何项用 IBL 的 k = roughness² / 2。
//   源 cube 必须带完整 mip 链（生成完调 GenerateTextureMipMaps）：很亮很小的光源（太阳、灯泡）要靠低分辨率 mip 摊开，
//   否则漫反射和粗糙镜面会出现一块一块的亮斑 / 暗斑。没有 mip 时不会出错，只是退化成稀疏采样。
class IBLImageBuilder
{
public:
	struct DiffuseGenerateParams {
		uint32_t gridSize = 16;			// 每面求和的像素数（在每个方向上），4~64
		uint32_t reserved = 0;
	};

	struct PrefilterGenerateParams {
		uint32_t sampleCount = 512;		// 每个输出像素的 GGX 采样数，16~4096
		uint32_t reserved = 0;
	};

	struct BRDFLUTGenerateParams {
		uint32_t sampleCount = 1024;
		uint32_t reserved = 0;
	};

public:
	IBLImageBuilder(
		const std::string& cubeDiffuseGenerateShaderPath = "shader/IBL/cubeDiffuseGenerate.comp",
		const std::string& cubePrefilterGenerateShaderPath = "shader/IBL/cubePrefilterGenerate.comp",
		const std::string& BRDFLUTGenerateShaderPath = "shader/IBL/BRDFLUTGenerate.comp"
	);

	// 漫反射辐照度 cube 很平滑，32 × 32 每面就够（大了只是浪费）
	void CalculateCubeDiffuse(
		const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		const std::shared_ptr<TextureCube>& _outputDiffuse,
		const std::shared_ptr<TextureCube>& _originCube,
		const DiffuseGenerateParams& params
	);

	void CalculateCubePrefilter(
		const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		const std::shared_ptr<TextureCube>& _outputPrefilter,
		const std::shared_ptr<TextureCube>& _originCube,
		const PrefilterGenerateParams& params
	);

	void CalculateBRDFLUT(
		const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		const std::shared_ptr<Texture2D>& _outpuBrdfLUT,
		const BRDFLUTGenerateParams& params
	);

private:
	ComputePipeline _iblDiffuseGenerateShader;
	ComputePipeline _iblPrefilterGenerateShader;
	ComputePipeline _BRDFLUTGenerateShader;
};
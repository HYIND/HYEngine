#pragma once

#include "VulkanRenderEngine/Base/ComputePipeline.h"


class IBLImageBuilder
{
public:
	struct DiffuseGenerateParams {
		uint32_t samplerCount;
		uint32_t frameIndex;
	};

	struct PrefilterGenerateParams {
		uint32_t samplerCount;
		uint32_t frameIndex;
	};

	struct BRDFLUTGenerateParams {
		uint32_t samplerCount = 100;
		uint32_t frameIndex = 567;
	};

public:
	IBLImageBuilder(
		const std::string& cubeDiffuseGenerateShaderPath = "shader/IBL/cubeDiffuseGenerate.comp",
		const std::string& cubePrefilterGenerateShaderPath = "shader/IBL/cubePrefilterGenerate.comp",
		const std::string& BRDFLUTGenerateShaderPath = "shader/IBL/BRDFLUTGenerate.comp"
	);

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
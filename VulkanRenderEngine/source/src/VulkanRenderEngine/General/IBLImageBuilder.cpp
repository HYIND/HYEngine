#include "vkstdafx.h"
#include "VulkanRenderEngine/General/IBLImageBuilder.h"

IBLImageBuilder::IBLImageBuilder(
	const std::string& cubeDiffuseGenerateShaderPath,
	const std::string& cubePrefilterGenerateShaderPath,
	const std::string& BRDFLUTGenerateShaderPath
)
{
	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
		config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
		config.computePath = cubeDiffuseGenerateShaderPath;

		config
			.AddUnifromBuffer(0)
			.AddStorageImage2DArray(1)
			.AddUnifromTexture(2);

		if (config.Validate())
			_iblDiffuseGenerateShader.Create(config);
	}

	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
		config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
		config.computePath = cubePrefilterGenerateShaderPath;

		config
			.AddUnifromBuffer(0)
			.AddStorageImage2DArray(1)
			.AddUnifromTexture(2);

		if (config.Validate())
			_iblPrefilterGenerateShader.Create(config);
	}

	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
		config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
		config.computePath = BRDFLUTGenerateShaderPath;

		config
			.AddUnifromBuffer(0)
			.AddStorageImage(1);

		if (config.Validate())
			_BRDFLUTGenerateShader.Create(config);
	}
}

void IBLImageBuilder::CalculateCubeDiffuse(
	const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
	const std::shared_ptr<TextureCube>& _outputDiffuse,
	const std::shared_ptr<TextureCube>& _originCube,
	const DiffuseGenerateParams& params
)
{
	if (!_outputDiffuse || _outputDiffuse->IsEmpty() || !_originCube || _originCube->IsEmpty())
		return;

	ComputeBindingRecord binding;
	auto paramsUBO = std::make_shared<UniformBlock>(sizeof(params));
	paramsUBO->WriteDataAsync(cmd, &params, sizeof(params));
	binding.SetUniformBlock(paramsUBO, 0);

	binding.SetStorageImage2DArray(_outputDiffuse, vk::ImageAspectFlagBits::eColor, 1);
	binding.SetUniformTextureCube(_originCube, vk::ImageAspectFlagBits::eColor, 2);

	_iblDiffuseGenerateShader.Bind(cmd, binding);
	cmd->dispatch(
		(_outputDiffuse->GetWidth() + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X,
		(_outputDiffuse->GetHeight() + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y,
		std::min(6u, _outputDiffuse->GetLayerCount())
	);
}

struct PrefilterGnerateRunTimeParams
{
	float roughness;
	uint32_t samplerCount;
	uint32_t frameIndex;
};

void IBLImageBuilder::CalculateCubePrefilter(
	const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
	const std::shared_ptr<TextureCube>& _outputPrefilter,
	const std::shared_ptr<TextureCube>& _originCube,
	const PrefilterGenerateParams& p
)
{
	if (!_outputPrefilter || _outputPrefilter->IsEmpty() || !_originCube || _originCube->IsEmpty())
		return;

	PrefilterGnerateRunTimeParams params{ .roughness = 0.f, .samplerCount = p.samplerCount, .frameIndex = p.frameIndex };
	auto paramsUBO = std::make_shared<UniformBlock>(sizeof(params));

	ComputeBindingRecord binding;
	binding.SetUniformTextureCube(_originCube, vk::ImageAspectFlagBits::eColor, 2);

	auto size = _outputPrefilter->GetSize();
	uint32_t maxLevel = _outputPrefilter->GetMaxLevel();


	for (uint32_t level = 0; level < maxLevel; level++) {
		uint32_t currW = std::max(1u, size.x >> level);
		uint32_t currH = std::max(1u, size.y >> level);

		params.roughness = Tool::LinearLerp(0.f, 1.f, float(level) / float(maxLevel - 1));
		params.roughness = std::clamp(params.roughness, 0.01f, 1.f);

		paramsUBO->WriteDataAsync(cmd, &params, sizeof(params));

		binding.SetUniformBlock(paramsUBO, 0);
		binding.SetStorageImage2DArrayLevel(_outputPrefilter, vk::ImageAspectFlagBits::eColor, level, 1, 1);

		_iblPrefilterGenerateShader.Bind(cmd, binding);
		cmd->dispatch(
			(currW + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X,
			(currH + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y,
			std::min(6u, _outputPrefilter->GetLayerCount())
		);
	}
}

void IBLImageBuilder::CalculateBRDFLUT(
	const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
	const std::shared_ptr<Texture2D>& _outpuBrdfLUT,
	const BRDFLUTGenerateParams& params
)
{
	if (!_outpuBrdfLUT || _outpuBrdfLUT->IsEmpty())
		return;

	ComputeBindingRecord binding;
	auto paramsUBO = std::make_shared<UniformBlock>(sizeof(params));
	paramsUBO->WriteDataAsync(cmd, &params, sizeof(params));
	binding.SetUniformBlock(paramsUBO, 0);

	binding.SetStorageImage(_outpuBrdfLUT, vk::ImageAspectFlagBits::eColor, 1);

	_BRDFLUTGenerateShader.Bind(cmd, binding);
	cmd->dispatch(
		(_outpuBrdfLUT->GetWidth() + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X,
		(_outpuBrdfLUT->GetHeight() + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y,
		1
	);
}

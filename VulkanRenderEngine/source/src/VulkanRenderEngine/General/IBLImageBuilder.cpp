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

// 和 cubePrefilterGenerate.comp 的 Params 一致（std140：4 个 32 位）
struct PrefilterGnerateRunTimeParams
{
	float roughness;
	uint32_t sampleCount;
	uint32_t level;
	uint32_t reserved;
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

	auto size = _outputPrefilter->GetSize();
	const uint32_t maxLevel = std::max(1u, _outputPrefilter->GetMaxLevel());

	for (uint32_t level = 0; level < maxLevel; level++) {
		const uint32_t currW = std::max(1u, size.x >> level);
		const uint32_t currH = std::max(1u, size.y >> level);

		// 第 0 级是镜面（roughness = 0，直接取源图），最后一级 roughness = 1
		PrefilterGnerateRunTimeParams params{
			.roughness = maxLevel > 1 ? float(level) / float(maxLevel - 1) : 0.0f,
			.sampleCount = p.sampleCount,
			.level = level,
			.reserved = 0,
		};

		// 每级一个 UBO：同一个命令缓冲里反复改写同一个 UBO，前面的 dispatch 可能读到后写的值
		auto paramsUBO = std::make_shared<UniformBlock>(sizeof(params));
		paramsUBO->WriteDataAsync(cmd, &params, sizeof(params));

		ComputeBindingRecord binding;
		binding.SetUniformBlock(paramsUBO, 0);
		binding.SetStorageImage2DArrayLevel(_outputPrefilter, vk::ImageAspectFlagBits::eColor, level, 1, 1);
		binding.SetUniformTextureCube(_originCube, vk::ImageAspectFlagBits::eColor, 2);

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

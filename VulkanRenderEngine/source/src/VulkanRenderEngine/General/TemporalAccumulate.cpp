#include "vkstdafx.h"
#include "VulkanRenderEngine/General/TemporalAccumulate.h"


namespace
{
	// 与 Temporal/TemporalHistoryClamp.comp 的 push constant 对应
	struct HistoryClampPushConstant
	{
		float clampGamma;
		float antilagStrength;
		float fastFrames;
		uint32_t debugMode;
		float maxLength;
	};
}

TemporalAccumulate::TemporalAccumulate(const std::string& temporalAccumulateShaderPath, const std::string& historyClampShaderPath)
{
	{
		ComputePipelineConfig clampConfig;
		clampConfig.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
		clampConfig.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
		clampConfig.computePath = historyClampShaderPath;
		clampConfig
			.AddStorageImage(1)			// 累积结果（读写）
			.AddStorageImage(2)			// 矩（读写）
			.AddUnifromTexture(3)		// 本帧快速历史
			.AddUnifromTexture(4)		// 深度
			.AddStorageImage(5)			// 调试热力图（裁剪量）
			.AddPushConstant(sizeof(HistoryClampPushConstant));
		if (clampConfig.Validate())
			_clampPipeline.Create(clampConfig);
	}

	ComputePipelineConfig config;
	config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
	config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
	config.computePath = temporalAccumulateShaderPath;

	config
		.AddCameraUnifromDataBinding()
		.AddUnifromBuffer(0)
		.AddStorageImage(1)
		.AddStorageImage(2)
		.AddUnifromTexture(3)
		.AddUnifromTexture(4)
		.AddUnifromTexture(5)
		.AddUnifromTexture(6)
		.AddUnifromTexture(7)
		.AddUnifromTexture(8)
		.AddUnifromTexture(9)
		.AddUnifromTexture(10)
		.AddUnifromTexture(11)
		.AddUnifromTexture(12)
		.AddStorageImage(13)		// 调试热力图
		.AddUnifromTexture(14)		// metallic / roughness（镜面信号的虚像重投影按粗糙度分配）
		.AddUnifromTexture(15)		// 快速历史（上一帧）
		.AddStorageImage(16);		// 快速历史（这一帧）

	if (config.Validate())
		_pipeline.Create(config);

}

void TemporalAccumulate::Execute(
	const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
	const std::shared_ptr<Texture2D>& originImage,
	const std::shared_ptr<Texture2D>& positionMap,
	const std::shared_ptr<Texture2D>& normalMap,
	const std::shared_ptr<Texture2D>& depthMap,
	const std::shared_ptr<Texture2D>& motionVectorMap,
	const std::shared_ptr<Texture2D>& prevPositionMap,
	const std::shared_ptr<Texture2D>& prevNormalMap,
	const std::shared_ptr<Texture2D>& prevDepthMap,
	const std::shared_ptr<Texture2D>& outputColorImage,
	const std::shared_ptr<Texture2D>& outputMomentImage,
	const std::shared_ptr<Texture2D>& historyColorImage,
	const std::shared_ptr<Texture2D>& historyMomentImage,
	const Params& params,
	std::shared_ptr<UniformBlock>& curCameraUBO,
	std::shared_ptr<UniformBlock>& prevCameraUBO,
	const std::shared_ptr<Texture2D>& metallicRoughnessMap
)
{
	auto size = outputColorImage->GetSize();

	// 快速历史：尺寸变了（或第一次）就重建，这一帧不读旧内容
	for (auto& tex : _fastHistory)
	{
		if (!tex || tex->IsEmpty() || tex->GetWidth() != size.x || tex->GetHeight() != size.y)
		{
			tex = std::make_shared<Texture2D>(size.x, size.y, vk::Format::eR16G16B16A16Sfloat);
			_fastValid = false;
		}
	}
	const auto& fastRead = _fastHistory[_fastWriteIndex ^ 1u];
	const auto& fastWrite = _fastHistory[_fastWriteIndex];
	fastRead->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
	fastRead->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
	fastWrite->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	Params shaderParams = params;
	shaderParams.fastHistoryValid = _fastValid ? 1u : 0u;

	auto paramsUBO = std::make_shared<UniformBlock>(sizeof(shaderParams));
	paramsUBO->WriteDataAsync(cmd, &shaderParams, sizeof(shaderParams));

	outputColorImage->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	vk::ClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	vk::ClearColorValue clearMoments = { 0.0f, 0.0f, 0.0f, 0.0f };
	vk::ImageSubresourceRange range;
	range.setAspectMask(vk::ImageAspectFlagBits::eColor)
		.setBaseArrayLayer(0)
		.setLayerCount(1)
		.setBaseMipLevel(0)
		.setLevelCount(1);
	cmd->clearColorImage(outputColorImage->GetImage(), vk::ImageLayout::eGeneral, clearColor, range);
	cmd->clearColorImage(outputMomentImage->GetImage(), vk::ImageLayout::eGeneral, clearMoments, range);

	// 调试输出：开调试时写 _debugTexture，否则绑 1×1 占位（shader 在 debugMode 为 0 时不写）
	std::shared_ptr<Texture2D> debugTarget;
	if (params.debugMode != 0)
	{
		if (!_debugTexture || _debugTexture->IsEmpty() || _debugTexture->GetWidth() != size.x || _debugTexture->GetHeight() != size.y)
			_debugTexture = std::make_shared<Texture2D>(size.x, size.y, vk::Format::eR16G16B16A16Sfloat);
		debugTarget = _debugTexture;
	}
	else
	{
		if (!_dummyDebugTexture)
			_dummyDebugTexture = std::make_shared<Texture2D>(1, 1, vk::Format::eR16G16B16A16Sfloat);
		debugTarget = _dummyDebugTexture;
	}
	_debugWritten = params.debugMode != 0;
	debugTarget->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	ComputeBindingRecord binding;
	binding.SetCameraUnifromData(curCameraUBO, prevCameraUBO);
	binding.SetUniformBlock(paramsUBO, 0);
	binding.SetStorageImage(outputColorImage, vk::ImageAspectFlagBits::eColor, 1);
	binding.SetStorageImage(outputMomentImage, vk::ImageAspectFlagBits::eColor, 2);
	binding.SetUniformTexture(originImage, vk::ImageAspectFlagBits::eColor, 3);
	binding.SetUniformTexture(historyColorImage, vk::ImageAspectFlagBits::eColor, 4);
	binding.SetUniformTexture(historyMomentImage, vk::ImageAspectFlagBits::eColor, 5);
	binding.SetUniformTexture(positionMap, vk::ImageAspectFlagBits::eColor, 6);
	binding.SetUniformTexture(normalMap, vk::ImageAspectFlagBits::eColor, 7);
	binding.SetUniformTexture(depthMap, vk::ImageAspectFlagBits::eDepth, 8);
	binding.SetUniformTexture(motionVectorMap, vk::ImageAspectFlagBits::eColor, 9);
	binding.SetUniformTexture(prevPositionMap, vk::ImageAspectFlagBits::eColor, 10);
	binding.SetUniformTexture(prevNormalMap, vk::ImageAspectFlagBits::eColor, 11);
	binding.SetUniformTexture(prevDepthMap, vk::ImageAspectFlagBits::eDepth, 12);
	binding.SetStorageImage(debugTarget, vk::ImageAspectFlagBits::eColor, 13);
	// 非镜面信号不读 binding 14，没给就拿法线图占位
	binding.SetUniformTexture(metallicRoughnessMap ? metallicRoughnessMap : normalMap, vk::ImageAspectFlagBits::eColor, 14);
	binding.SetUniformTexture(fastRead, vk::ImageAspectFlagBits::eColor, 15);
	binding.SetStorageImage(fastWrite, vk::ImageAspectFlagBits::eColor, 16);

	_pipeline.Bind(cmd, binding);
	cmd->dispatch((size.x + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (size.y + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

	// ---------------- 历史裁剪：用本帧快速历史的 3×3 邻域裁累积结果（要等上面写完，单独一次 dispatch） ----------------
	outputColorImage->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
	outputMomentImage->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
	fastWrite->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
	{
		ComputeBindingRecord clampBinding;
		clampBinding.SetStorageImage(outputColorImage, vk::ImageAspectFlagBits::eColor, 1);
		clampBinding.SetStorageImage(outputMomentImage, vk::ImageAspectFlagBits::eColor, 2);
		clampBinding.SetUniformTexture(fastWrite, vk::ImageAspectFlagBits::eColor, 3);
		clampBinding.SetUniformTexture(depthMap, vk::ImageAspectFlagBits::eDepth, 4);
		clampBinding.SetStorageImage(debugTarget, vk::ImageAspectFlagBits::eColor, 5);

		HistoryClampPushConstant pc{
			.clampGamma = params.varianceClipGamma,
			.antilagStrength = std::max(0.0f, params.antilagStrength),
			.fastFrames = std::max(1.0f, params.fastHistoryLength),
			.debugMode = params.debugMode,
			.maxLength = float(std::max(params.maxAccumulateCount, 1u))
		};
		_clampPipeline.SetPushConstants(cmd, &pc, sizeof(pc));
		_clampPipeline.Bind(cmd, clampBinding);
		cmd->dispatch((size.x + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (size.y + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);
	}
	// 累积结果之后会被拷进历史、再被空域滤波读：交给调用方的绑定 / 拷贝去转换布局，这里只保证写完
	outputColorImage->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
	outputMomentImage->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);

	// 这一帧写的那张下一帧读
	_fastWriteIndex ^= 1u;
	_fastValid = true;

}

std::shared_ptr<Texture2D> TemporalAccumulate::GetDebugTexture() const
{
	return _debugWritten ? _debugTexture : nullptr;
}

#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/AutoExposurePass.h"
#include "VulkanRenderEngine/GlobalConfig.h"

constexpr int work_size_x = 16;
constexpr int work_size_y = 16;

struct HistogramSSBOParams
{
	glm::ivec2 screenSize;
	uint32_t bins[256] = { 0 };
};

AutoExposurePass::AutoExposurePass(const std::string& computeShaderPath)
{
	ComputePipelineConfig config;
	config.AddDefineMacro("work_size_x", work_size_x);
	config.AddDefineMacro("work_size_y", work_size_y);
	config.AddDefineMacro("MIN_EV", GlobalConfig::AutoExposure_MIN_EV);
	config.AddDefineMacro("MAX_EV", GlobalConfig::AutoExposure_MAX_EV);
	config.computePath = computeShaderPath;

	config
		.AddStorageBuffer(0)
		.AddStorageImage(1);

	if (config.Validate())
		_shader.Create(config);

	_paramsSSBO = std::make_shared<StorageBlock>(sizeof(HistogramSSBOParams));
	_binding.SetStorageBlock(_paramsSSBO, 0);
}

bool AutoExposurePass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return state.option.flags.autoExposureOn && state.framebuffer.width > 0 && state.framebuffer.height > 0;
}

void AutoExposurePass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!ShouldExecute(registry, state))
		return;

	HistogramSSBOParams params
	{
		.screenSize = glm::ivec2(state.framebuffer.width, state.framebuffer.height)
	};
	_paramsSSBO->WriteData(&params, sizeof(HistogramSSBOParams));
}

void AutoExposurePass::Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{

	auto sceneColorBuffer = ctx.GetExternal(0);

	auto cmd = VKCONTEXT->GetCommandBuffer();

	_binding.SetStorageImage(sceneColorBuffer, 1);

	_shader.Bind(cmd, _binding);
	cmd->dispatch((state.framebuffer.width + work_size_x - 1) / work_size_x, (state.framebuffer.height + work_size_y - 1) / work_size_y, 1);
	_paramsSSBO->Barrier(cmd, BufferUsage::StorageWrite, BufferUsage::TransferRead);

	std::vector<uint32_t> bins;
	bins.resize(256, 0);
	if (!_paramsSSBO->GetBuffer()->Readback(cmd, bins.data(), bins.size() * sizeof(uint32_t), sizeof(glm::ivec2)))
		return;

	static std::once_flag onceFlag;
	static float lumenTable[256];
	std::call_once(onceFlag, [&]()-> void {
		for (int i = 0; i < 256; i++)
		{
			float normalized = i / 255.0;
			float ev = GlobalConfig::AutoExposure_MIN_EV + normalized * GlobalConfig::AutoExposure_EV_RANGE;
			lumenTable[i] = pow(2.0, ev);
		}
		});

	// 统计总像素数和总的光照
	double sumLuminance = 0.0;
	uint32_t samplerCount = 0;
	uint32_t validCount = 0;

	for (int i = 0; i < 256; i++)
	{
		sumLuminance += lumenTable[i] * bins[i];
		samplerCount += bins[i];
	}
	validCount = samplerCount;

	static bool skip = false;

	if (!skip)
	{

		// 剔除无效的亮度范围（去掉极端值）
		uint32_t cumulative = 0;
		float lowPercentile = 0.1;   // 忽略最暗 10%
		float highPercentile = 0.9;  // 忽略最亮 10%
		uint32_t lowThreshold = samplerCount * lowPercentile;
		uint32_t highThreshold = samplerCount * highPercentile;

		for (int i = 0; i < 256; i++)
		{
			if (bins[i] == 0) continue;

			uint32_t newCumulative = cumulative + bins[i];
			if (!(cumulative > lowThreshold && newCumulative < highThreshold))
			{
				if (cumulative >= highThreshold || newCumulative <= lowThreshold)
				{
					int count = bins[i];
					sumLuminance -= lumenTable[i] * count;
					validCount -= count;
				}
				else
				{
					if (cumulative < lowThreshold && newCumulative >= lowThreshold)
					{
						int count = lowThreshold - cumulative;
						sumLuminance -= lumenTable[i] * count;
						validCount -= count;
					}
					else if (cumulative < highThreshold && newCumulative >= highThreshold)
					{
						int count = newCumulative - highThreshold;
						sumLuminance -= lumenTable[i] * count;
						validCount -= count;
					}
				}
			}
			cumulative = newCumulative;
		}
	}

	// 安全处理：如果没有有效像素，用默认值
	float avgLuminance = (validCount > 0 && sumLuminance > 0) ? (sumLuminance / validCount) : 0.18;

	// 计算曝光值（映射到中灰色）
	float targetLuminance = 0.18; // 18% 灰
	float targetExposure = targetLuminance / avgLuminance;

	// 限制曝光范围（防止闪烁/过激）
	float targetEV = log2(targetExposure);

	float currentEV;
	float deltaSecond = float(state.renderRecord.currentRenderMicroTimeStamp - state.renderRecord.prevRenderMicroTimeStamp) / 1000000.f;
	if (deltaSecond < 0.f)
	{
		currentEV = targetEV;
	}
	else
	{
		constexpr float AdaptationSpeed = 4.0f;  // 适应速度，单位 EV/秒
		float maxDeltaEV = AdaptationSpeed * deltaSecond;
		float deltaEV = targetEV - state.renderRecord.prevEV100;
		deltaEV = std::clamp(deltaEV, -maxDeltaEV, maxDeltaEV);
		currentEV = state.renderRecord.prevEV100 + deltaEV;
	}

	currentEV = std::clamp(currentEV, GlobalConfig::AutoExposure_MIN_EV, GlobalConfig::AutoExposure_MAX_EV);

	float finalExposure = pow(2.0f, currentEV);
	state.option.postProcessParams.EV100 = currentEV;

	//std::cout << std::format("exposure = {}\n", state.common.EV100);
}

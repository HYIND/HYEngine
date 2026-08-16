#include "OpenGLRenderEngine/RenderPass/AutoExposurePass.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
#include <random>
#include <format>
#include <glm/gtc/matrix_transform.hpp>

constexpr int work_size_x = 16;
constexpr int work_size_y = 16;

AutoExposurePass::AutoExposurePass(const std::string& histogramComputerShaderPath)
{
	_histogramShader.AddDefineMacro("work_size_x", work_size_x);
	_histogramShader.AddDefineMacro("work_size_y", work_size_y);
	_histogramShader.AddDefineMacro("MIN_EV", OpenGLRenderConfig::AutoExposure_MIN_EV);
	_histogramShader.AddDefineMacro("MAX_EV", OpenGLRenderConfig::AutoExposure_MAX_EV);
	_histogramShader.CompileFromFile(histogramComputerShaderPath);
}

bool AutoExposurePass::ShouldExecute(RenderState& state) const
{
	return state.option.flags.autoExposureOn && state.framebuffer.width > 0 && state.framebuffer.height > 0;
}

void AutoExposurePass::Execute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{

	auto sceneColorBuffer = ctx.GetExternal(0);

	glm::ivec2 screenSize = glm::ivec2(state.framebuffer.width, state.framebuffer.height);

	auto histogramBuffer = _histogramShader.TryGetSSBO("HistogramBuffer");
	if (!histogramBuffer)	return;
	std::vector<unsigned int> bins;
	bins.resize(256, 0);
	histogramBuffer->WriteData(bins.data(), bins.size() * sizeof(unsigned int), 0);

	_histogramShader.Use();
	_histogramShader.setIVec2("screenSize", screenSize);

	glBindImageTexture(0, sceneColorBuffer->GetID(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
	glDispatchCompute((state.framebuffer.width + work_size_x - 1) / work_size_x, (state.framebuffer.height + work_size_y - 1) / work_size_y, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	glBindBuffer(GL_SHADER_STORAGE_BUFFER, histogramBuffer->GetID());
	//void* mappedPtr = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, bins.size() * sizeof(unsigned int), GL_MAP_READ_BIT);
	//if (!mappedPtr)
	//	return;
	//memcpy(bins.data(), mappedPtr, bins.size() * sizeof(unsigned int));
	//glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);

	glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bins.size() * sizeof(unsigned int), bins.data());

	static std::once_flag onceFlag;
	static float lumenTable[256];
	std::call_once(onceFlag, [&]()-> void {
		for (int i = 0; i < 256; i++)
		{
			float normalized = i / 255.0;
			float ev = OpenGLRenderConfig::AutoExposure_MIN_EV + normalized * OpenGLRenderConfig::AutoExposure_EV_RANGE;
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

	currentEV = std::clamp(currentEV, OpenGLRenderConfig::AutoExposure_MIN_EV, OpenGLRenderConfig::AutoExposure_MAX_EV);

	float finalExposure = pow(2.0f, currentEV);
	state.option.postProcessParams.EV100 = currentEV;

	//std::cout << std::format("exposure = {}\n", state.common.EV100);
}

#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/GlobalPostProcessPass.h"




struct alignas(16) Params {
	float exposureValue;
	float gammaValue;
	uint32_t gammaEnable;
	uint32_t bloomEnable;
	uint32_t filpY;
};

GlobalPostProcessPass::GlobalPostProcessPass(const std::string& computeShaderPath)
{
	ComputePipelineConfig config;
	config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
	config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
	config.computePath = computeShaderPath;

	config
		.AddStorageImage(0)
		.AddUnifromTexture(1)
		.AddUnifromTexture(2)
		.AddPushConstant(sizeof(Params));

	if (config.Validate())
		_shader.Create(config);

}

void GlobalPostProcessPass::Draw(
	std::shared_ptr<Texture2D> outPut,
	std::shared_ptr<Texture2D> colorBuffer,
	std::shared_ptr<Texture2D> bloomBlurMap,
	bool bloom_on, bool gamma_on, bool flipY,
	float exposureValue, float gammaValue
)
{
	if (!outPut
		|| outPut->IsEmpty()
		|| !colorBuffer
		|| colorBuffer->IsEmpty()
		|| !bloomBlurMap
		|| bloomBlurMap->IsEmpty()
		)
		return;

	uint32_t width = outPut->GetWidth();
	uint32_t height = outPut->GetHeight();

	Params params{ .exposureValue = exposureValue, .gammaValue = gammaValue, .gammaEnable = gamma_on, .bloomEnable = bloom_on, .filpY = flipY };

	ComputeBindingRecord binding;
	binding.SetStorageImage(outPut, vk::ImageAspectFlagBits::eColor, 0);
	binding.SetUniformTexture(colorBuffer, vk::ImageAspectFlagBits::eColor, 1);
	if (bloom_on) binding.SetUniformTexture(bloomBlurMap, vk::ImageAspectFlagBits::eColor, 2);

	auto cmd = VKCONTEXT->GetCommandBuffer();

	_shader.SetPushConstants(cmd, &params, sizeof(params));

	_shader.Bind(cmd, binding);
	cmd->dispatch((width + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (height + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

	cmd->SubmitNowAndWait();
}

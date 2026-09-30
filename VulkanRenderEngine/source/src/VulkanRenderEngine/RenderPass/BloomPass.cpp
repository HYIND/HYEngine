#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/BloomPass.h"

constexpr uint32_t work_size_x = 16;
constexpr uint32_t work_size_y = 16;

std::array<float, 5> s_weight = {
	1.f / 16.f,
	4.f / 16.f,
	6.f / 16.f,
	4.f / 16.f,
	1.f / 16.f
};

//const std::array<float, 9> s_weight = {
//	1.f,
//	1.f,
//	1.f,
//	1.f,
//	1.f,
//	1.f,
//	1.f,
//	1.f,
//	1.f
//};

//const std::array<float, 9> s_weight = {
//	0.02763055f,
//	0.06628225f,
//	0.12383154f,
//	0.18017382f,
//	0.20416369f,
//	0.18017382f,
//	0.12383154f,
//	0.06628225f,
//	0.02763055f
//};

struct alignas(16) Weight
{
	float value;
	float padding[3];
};

struct alignas(16) Params
{
	std::array<Weight, 5> weight;
	Params(const std::array<float, 5>& weights = s_weight)
	{
		for (int i = 0; i < weight.size(); i++)
			weight[i].value = weights[i];
	}
};

BloomPass::BloomPass(
	const std::string& bloomDownSampleShaderPath,
	const std::string& bloomUpSampleShaderPath
)
{

	{
		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", work_size_x);
		config.AddDefineMacro("work_size_y", work_size_y);
		config.computePath = bloomDownSampleShaderPath;

		config
			.AddUnifromBuffer(0)
			.AddStorageImage(1)
			.AddUnifromTexture(2);

		if (config.Validate())
			_bloomDownSampleShader.Create(config);
	}

	{

		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", work_size_x);
		config.AddDefineMacro("work_size_y", work_size_y);
		config.computePath = bloomUpSampleShaderPath;

		config
			.AddUnifromBuffer(0)
			.AddStorageImage(1)
			.AddUnifromTexture(2);

		if (config.Validate())
			_bloomUpSampleShader.Create(config);
	}

}

void BloomPass::Draw(std::shared_ptr<Texture2D>& brightColorBuffer, std::vector<std::shared_ptr<Texture2D>>& bloomMipBuffers)
{
	if (!brightColorBuffer || brightColorBuffer->IsEmpty())
		return;

	auto cmd = VKCONTEXT->GetCommandBuffer();

	if (!Texture2D::CopyTextureAsync(cmd, brightColorBuffer, bloomMipBuffers[0]))
		return;

	Params params;
	auto paramsUBO = std::make_shared<UniformBlock>(sizeof(params));
	paramsUBO->WriteDataAsync(cmd, &params, sizeof(params));
	paramsUBO->Barrier(cmd, BufferUsage::UniformRead);

	ComputeBindingRecord binding;
	binding.SetUniformBlock(paramsUBO, 0);

	bool first_iteration = true;

	auto size = bloomMipBuffers[0]->GetSize();
	for (uint32_t level = 1; level < bloomMipBuffers.size(); level++) {

		uint32_t prevLevel = level - 1;
		uint32_t curLevel = level;

		auto& prevImage = bloomMipBuffers[prevLevel];
		auto& curImage = bloomMipBuffers[curLevel];

		auto prevSize = prevImage->GetSize();
		auto curSize = curImage->GetSize();

		binding.SetStorageImage(curImage, vk::ImageAspectFlagBits::eColor, 1);
		binding.SetUniformTexture(prevImage, vk::ImageAspectFlagBits::eColor, 2);
		_bloomDownSampleShader.Bind(cmd, binding);
		cmd->dispatch((curSize.x + work_size_x - 1) / work_size_x, (curSize.y + work_size_y - 1) / work_size_y, 1);

		curImage->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
	}

	for (uint32_t level = bloomMipBuffers.size() - 1; level > 0; level--) {

		uint32_t prevLevel = level;
		uint32_t curLevel = level - 1;

		auto& prevImage = bloomMipBuffers[prevLevel];
		auto& curImage = bloomMipBuffers[curLevel];

		auto prevSize = prevImage->GetSize();
		auto curSize = curImage->GetSize();

		binding.SetStorageImage(curImage, vk::ImageAspectFlagBits::eColor, 1);
		binding.SetUniformTexture(prevImage, vk::ImageAspectFlagBits::eColor, 2);
		_bloomUpSampleShader.Bind(cmd, binding);
		cmd->dispatch((curSize.x + work_size_x - 1) / work_size_x, (curSize.y + work_size_y - 1) / work_size_y, 1);

		curImage->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
	}

	cmd->SubmitNowAndWait();
}

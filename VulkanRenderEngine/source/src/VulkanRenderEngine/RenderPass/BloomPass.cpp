#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/BloomPass.h"

constexpr uint32_t work_size_x = 16;
constexpr uint32_t work_size_y = 16;

struct alignas(16) Params
{
	std::array<float, 5> weight = { 0.2270270270, 0.1945945946, 0.1216216216, 0.0540540541, 0.0162162162 };
	uint32_t horizontal;
};

static Params params;

BloomPass::BloomPass(const std::string& computeShaderPath, uint32_t width, uint32_t height)
	:_width(width), _height(height)
{
	ComputePipelineConfig config;
	config.AddDefineMacro("work_size_x", work_size_x);
	config.AddDefineMacro("work_size_y", work_size_y);
	config.computePath = computeShaderPath;

	config
		.AddStorageImage(0)
		.AddUnifromTexture(1)
		.AddPushConstant(sizeof(Params));

	if (config.Validate())
		_bloomBlurShader.Create(config);

	init();
}

void BloomPass::Draw(std::shared_ptr<Texture2D>& brightColorBuffer)
{
	if (!brightColorBuffer || brightColorBuffer->IsEmpty())
		return;

	auto cmd = VKCONTEXT->GetCommandBuffer();

	bool horizontal = true;
	bool first_iteration = true;

	uint32_t count = 10;
	std::shared_ptr<Texture2D> DrawImage = _pingpongColorBuffers[0];
	std::shared_ptr<Texture2D> SampleImage = brightColorBuffer;
	for (uint32_t i = 0; i < count; i++)
	{
		params.horizontal = horizontal;

		DrawImage->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Write);
		SampleImage->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);

		ComputeBindingRecord bloomBlurBinding;
		bloomBlurBinding.SetStorageImage(DrawImage, vk::ImageAspectFlagBits::eColor, 0);
		bloomBlurBinding.SetUniformTexture(SampleImage, vk::ImageAspectFlagBits::eColor, 1);
		_bloomBlurShader.SetPushConstants(cmd, &params, sizeof(params));

		_bloomBlurShader.Bind(cmd, bloomBlurBinding);
		cmd->dispatch((_width + work_size_x - 1) / work_size_x, (_height + work_size_y - 1) / work_size_y, 1);

		_outPutTarget = DrawImage;

		horizontal = !horizontal;
		if (first_iteration)
		{
			first_iteration = false;
			DrawImage = _pingpongColorBuffers[1];
			SampleImage = _pingpongColorBuffers[0];
		}
		else
			std::swap(DrawImage, SampleImage);
	}

	cmd->SubmitNowAndWait();
}

std::shared_ptr<Texture2D> BloomPass::GetBloomBlurMap()
{
	return _outPutTarget;
}

void BloomPass::Resize(uint32_t newWidth, uint32_t newHeight)
{
	if (_width == newWidth && _height == newHeight)
		return;

	_width = newWidth;
	_height = newHeight;
	init();
}

void BloomPass::init()
{
	for (auto& tex : _pingpongColorBuffers)
	{
		if (!tex)
			tex = std::make_shared<Texture2D>(_width, _height, vk::Format::eR16G16B16A16Sfloat);
		else
			tex->Resize(_width, _height);
	}
	_outPutTarget = _pingpongColorBuffers[0];
}

#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/CombinPass.h"

constexpr uint32_t work_size_x = 16;
constexpr uint32_t work_size_y = 16;

CombinPass::CombinPass(const std::string& computerShaderPath)
{

	ComputePipelineConfig config;
	config.AddDefineMacro("COMBIN_MODE", 0);
	config.AddDefineMacro("work_size_x", work_size_x);
	config.AddDefineMacro("work_size_y", work_size_y);
	config.computePath = computerShaderPath;

	config
		.AddStorageImage(0)
		.AddStorageImage(1)
		.AddUnifromVariableTextureArray(2, 20)
		.AddPushConstant(sizeof(uint32_t));

	if (config.Validate())
		_shader.Create(config);
}

void CombinPass::Draw(std::shared_ptr<Texture2D>& destColorTexture, std::shared_ptr<Texture2D>& destBrightTexture, const std::vector<std::shared_ptr<Texture2D>>& colorBuffers)
{
	if (
		!destColorTexture
		|| !destBrightTexture
		|| destColorTexture->IsEmpty()
		|| destBrightTexture->IsEmpty()
		|| colorBuffers.empty()
		)
		return;

	for (auto& tex : colorBuffers)
		if (!tex) return;

	uint32_t width = destColorTexture->GetWidth();
	uint32_t height = destColorTexture->GetHeight();

	auto cmd = VKCONTEXT->GetCommandBuffer();

	ComputeBindingRecord _binding;
	_binding.SetStorageImage(destColorTexture, 0);
	_binding.SetStorageImage(destBrightTexture, 1);
	_binding.SetUniformTextureArray(colorBuffers, 2);

	uint32_t count = std::min(Max_Color_Buffer_Count, (uint32_t)colorBuffers.size());

	_shader.Bind(cmd, _binding);
	_shader.SetPushConstants(cmd, &count, sizeof(count));

	cmd->dispatch((width + work_size_x - 1) / work_size_x, (height + work_size_y - 1) / work_size_y, 1);

	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
}


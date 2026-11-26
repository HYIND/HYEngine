#include "vkstdafx.h"
#include "VulkanRenderEngine/VKRenderer2D.h"

#include "VulkanRenderEngine/VKContext.h"
#include "VulkanRenderEngine/Base/Texture2D.h"
#include "VulkanRenderEngine/Base/GraphicsPipeline.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"

using namespace VKRenderContext;

namespace
{
	vk::PipelineColorBlendAttachmentState MakeBlendState(BlendMode2D mode)
	{
		vk::PipelineColorBlendAttachmentState state;
		state.setBlendEnable(vk::True)
			.setColorBlendOp(vk::BlendOp::eAdd)
			.setAlphaBlendOp(vk::BlendOp::eAdd)
			.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

		switch (mode)
		{
		case BlendMode2D::Additive:
			state.setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
				.setDstColorBlendFactor(vk::BlendFactor::eOne)
				.setSrcAlphaBlendFactor(vk::BlendFactor::eZero)
				.setDstAlphaBlendFactor(vk::BlendFactor::eOne);
			break;
		case BlendMode2D::Multiply:
			// 着色器输出 mix(1, src, a)，这里 dst * src
			state.setSrcColorBlendFactor(vk::BlendFactor::eDstColor)
				.setDstColorBlendFactor(vk::BlendFactor::eZero)
				.setSrcAlphaBlendFactor(vk::BlendFactor::eZero)
				.setDstAlphaBlendFactor(vk::BlendFactor::eOne);
			break;
		case BlendMode2D::Premultiplied:
			state.setSrcColorBlendFactor(vk::BlendFactor::eOne)
				.setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
				.setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
				.setDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha);
			break;
		case BlendMode2D::Alpha:
		default:
			state.setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
				.setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
				.setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
				.setDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha);
			break;
		}
		return state;
	}
}

VKRenderer2D::VKRenderer2D(std::string vertexShaderPath, std::string fragmentShaderPath)
	: _vertexShaderPath(std::move(vertexShaderPath)), _fragmentShaderPath(std::move(fragmentShaderPath))
{}

VKRenderer2D::~VKRenderer2D()
{
	Release();
}

void VKRenderer2D::Release()
{
	std::lock_guard<std::mutex> lock(_mutex);
	for (auto& pipeline : _pipelines)
		pipeline.reset();
	_pipelinesReady = false;
	_format = vk::Format::eUndefined;
	_instanceBuffer.reset();
	_whiteTexture.reset();
}

bool VKRenderer2D::EnsurePipelines(vk::Format format)
{
	if (_pipelinesReady && _format == format)
		return true;

	if (!VKCONTEXT || !VKCONTEXT->GetDevice())
		return false;

	_pipelinesReady = false;
	_format = format;

	for (size_t i = 0; i < _pipelines.size(); ++i)
	{
		const auto mode = static_cast<BlendMode2D>(i);

		GraphicsPipelineConfig config;
		config.vertexPath = _vertexShaderPath;
		config.fragmentPath = _fragmentShaderPath;
		if (mode == BlendMode2D::Multiply)
			config.AddDefineMacro("BLEND_MULTIPLY", "1");

		config.AddColorAttachment(format, MakeBlendState(mode));
		config.SetDepthStencilAttachmentFormat(vk::Format::eUndefined);		// 2D 层不用深度
		config.SetDepthTest(false, false, vk::CompareOp::eAlways);
		config.SetCullMode(vk::CullModeFlagBits::eNone);					// 翻转后的四边形绕序会反
		config.SetTopology(vk::PrimitiveTopology::eTriangleList);
		config.AddStorageBuffer(0);											// 实例
		config.AddUnifromVariableTextureArray(1, kMaxTexturesPerBatch);		// 本批次的贴图
		config.AddPushConstant(sizeof(PushConstants));

		// 顶点由 gl_VertexIndex 生成，不需要顶点输入（所以不走 Validate，免得打印无顶点输入的警告）
		auto pipeline = std::make_unique<GraphicsPipeline>();
		if (!pipeline->Create(config) || !pipeline->IsValid())
		{
			std::cerr << std::format("[ Renderer2D ] 创建 2D 管线失败（blend = {}）\n", i);
			return false;
		}
		_pipelines[i] = std::move(pipeline);
	}

	if (!_instanceBuffer)
		_instanceBuffer = std::make_shared<StorageBlock>(sizeof(GPUInstance) * 256);

	if (!_whiteTexture)
	{
		TextureConfig config;
		config.minFilter = vk::Filter::eNearest;
		config.magFilter = vk::Filter::eNearest;
		_whiteTexture = std::make_shared<Texture2D>(1u, 1u, vk::Format::eR8G8B8A8Unorm, config, 1u);
		uint8_t white[4] = { 255, 255, 255, 255 };
		_whiteTexture->UpdateTextureData(white);
	}

	_pipelinesReady = true;
	return true;
}

void VKRenderer2D::BuildBatches(const Render2DFrame& frame, const View2D& view)
{
	// 调试线换算成细长四边形，宽度按屏幕像素
	_lineItems.clear();
	if (!frame.lines.empty())
	{
		const glm::vec2 ppu = view.PixelsPerUnit();
		const float unitsPerPixel = 2.0f / std::max(ppu.x + ppu.y, 1e-6f);
		uint32_t sequence = 0;
		for (const auto& line : frame.lines)
		{
			const glm::vec2 d = line.to - line.from;
			const float length = glm::length(d);
			if (length <= 1e-6f)
				continue;

			const glm::vec2 normal = glm::vec2(-d.y, d.x) / length;
			const float width = std::max(line.width, 0.5f) * unitsPerPixel;

			Sprite2DDrawItem item;
			item.origin = line.from - normal * (width * 0.5f);
			item.axisX = d;
			item.axisY = normal * width;
			item.color = line.color;
			item.layer = line.layer;
			item.order = 0;
			item.sequence = sequence++;
			_lineItems.push_back(item);
		}
	}

	_sorted.clear();
	_sorted.reserve(frame.sprites.size() + _lineItems.size());
	for (const auto& item : frame.sprites)
		_sorted.push_back(&item);
	for (const auto& item : _lineItems)
		_sorted.push_back(&item);

	std::stable_sort(_sorted.begin(), _sorted.end(), [](const Sprite2DDrawItem* a, const Sprite2DDrawItem* b)
		{
			if (a->layer != b->layer) return a->layer < b->layer;
			if (a->order != b->order) return a->order < b->order;
			return a->sequence < b->sequence;
		});

	_instances.clear();
	_instances.reserve(_sorted.size());
	_batches.clear();

	std::unordered_map<const Texture2D*, uint32_t> textureSlots;
	Batch* current = nullptr;

	for (const auto* item : _sorted)
	{
		const std::shared_ptr<Texture2D>& texture = (item->texture && !item->texture->IsEmpty()) ? item->texture : _whiteTexture;

		const bool needNewBatch = !current
			|| current->blend != item->blend
			|| (current->textures.size() >= kMaxTexturesPerBatch && !textureSlots.contains(texture.get()));
		if (needNewBatch)
		{
			_batches.push_back(Batch{});
			current = &_batches.back();
			current->blend = item->blend;
			current->firstInstance = static_cast<uint32_t>(_instances.size());
			textureSlots.clear();
		}

		uint32_t slot = 0;
		if (auto it = textureSlots.find(texture.get()); it != textureSlots.end())
			slot = it->second;
		else
		{
			slot = static_cast<uint32_t>(current->textures.size());
			current->textures.push_back(texture);
			textureSlots.emplace(texture.get(), slot);
		}

		GPUInstance instance{};
		instance.origin = item->origin;
		instance.axisX = item->axisX;
		instance.axisY = item->axisY;
		instance.texIndex = slot;
		instance.flags = 0;
		instance.uvRect = item->uvRect;
		instance.color = item->color;
		if (item->blend == BlendMode2D::Premultiplied)
			instance.color = glm::vec4(glm::vec3(item->color) * item->color.a, item->color.a);
		_instances.push_back(instance);
		++current->instanceCount;
	}
}

void VKRenderer2D::Render(const std::shared_ptr<Texture2D>& target, const Render2DFrame& frame)
{
	if (!target || target->IsEmpty())
		return;
	if (frame.sprites.empty() && frame.lines.empty())
		return;

	std::lock_guard<std::mutex> lock(_mutex);

	if (!EnsurePipelines(target->GetFormat()))
		return;

	const uint32_t width = target->GetWidth();
	const uint32_t height = target->GetHeight();
	const View2D view = ComputeView2D(frame.camera, width, height);

	BuildBatches(frame, view);
	if (_instances.empty() || _batches.empty())
		return;

	// 1. 上传实例数据
	{
		auto cmd = VKCONTEXT->GetCommandBuffer();
		if (!cmd)
			return;

		const uint64_t requiredBytes = static_cast<uint64_t>(_instances.size()) * sizeof(GPUInstance);
		if (requiredBytes > _instanceBuffer->GetSize())
		{
			const uint64_t grownBytes = std::max<uint64_t>(requiredBytes, _instanceBuffer->GetSize() + _instanceBuffer->GetSize() / 2);
			_instanceBuffer->SetSizeAsync(cmd, grownBytes, false);
		}
		_instanceBuffer->WriteDataAsync(cmd, _instances.data(), requiredBytes);
		if (cmd->IsRecording())
			cmd->SubmitNowAndWait();
	}

	// 2. 叠加绘制
	auto cmd = VKCONTEXT->GetCommandBuffer();
	if (!cmd)
		return;

	cmd->Begin();

	target->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);

	// 贴图在开始渲染之前统一切到可采样，避免绑定时在 rendering 内部插 barrier
	{
		std::unordered_set<const Texture2D*> transitioned;
		for (auto& batch : _batches)
		{
			for (auto& texture : batch.textures)
			{
				if (transitioned.insert(texture.get()).second)
					texture->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Read);
			}
		}
	}

	DynamicRenderInfo renderInfo{};
	renderInfo.SetRenderArea(width, height);
	renderInfo.AddColorAttachment(
		target->GetImageView(vk::ImageAspectFlagBits::eColor),
		vk::AttachmentLoadOp::eLoad,
		vk::ClearColorValue{ std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f } });

	cmd->beginRendering(renderInfo);
	cmd->setDynamicViewport(width, height);

	PushConstants constants{};
	constants.viewProj = view.viewProj;
	constants.targetSize = glm::vec2(static_cast<float>(width), static_cast<float>(height));
	constants.frameFlags = frame.camera.pixelSnap ? 1u : 0u;

	for (auto& batch : _batches)
	{
		auto& pipeline = _pipelines[static_cast<size_t>(batch.blend)];
		if (!pipeline || batch.instanceCount == 0)
			continue;

		GraphicsBindingRecord binding;
		binding.SetStorageBlock(_instanceBuffer, 0);
		binding.SetUniformTextureArray(batch.textures, vk::ImageAspectFlagBits::eColor, 1);

		pipeline->Bind(cmd, binding);
		pipeline->SetPushConstants(cmd, &constants, sizeof(constants));
		cmd->draw(6, batch.instanceCount, 0, batch.firstInstance);
	}

	cmd->endRendering();

	target->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Read);

	cmd->End();
	cmd->SubmitNowAndWait();
}

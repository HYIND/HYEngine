#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/EditorPickPass.h"
#include "VulkanRenderEngine/Base/Texture2D.h"
#include "VulkanRenderEngine/GlobalConfig.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace
{
	using RequestPtr = std::shared_ptr<const EditorPickRequest>;
	constexpr const char* kRequestKey = "editorPickRequest";

	// 和 pickReadback.comp 的 push_constant 一致
	struct alignas(16) PickParams
	{
		glm::ivec2 rectMin{ 0 };
		glm::ivec2 rectSize{ 0 };
		uint32_t wantPosition = 0;
		uint32_t pad[3] = {};
	};
}

EditorPickPass::EditorPickPass(const std::string& computeShaderPath)
{
	ComputePipelineConfig config;
	config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
	config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
	config.computePath = computeShaderPath;
	config
		.AddUnifromTexture(0)		// 实体 ID 缓冲
		.AddUnifromTexture(1)		// gPosition
		.AddStorageBuffer(2)		// 输出
		.AddPushConstant(sizeof(PickParams));

	if (config.Validate())
		_shader.Create(config);
}

void EditorPickPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	RequestPtr request;
	if (_channel)
	{
		if (auto taken = _channel->TakePick())
			request = std::make_shared<const EditorPickRequest>(*taken);
	}
	registry.Store(kRequestKey, std::move(request));
}

bool EditorPickPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	auto* request = registry.TryLoad<RequestPtr>(kRequestKey);
	return _channel && request && *request;
}

void EditorPickPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	auto* holder = registry.TryLoad<RequestPtr>(kRequestKey);
	if (!_channel || !holder || !*holder)
		return;
	const EditorPickRequest request = **holder;
	registry.Store(kRequestKey, RequestPtr{});

	EditorPickResult result;
	result.requestId = request.id;
	result.frameIndex = state.renderRecord.frameIndex;

	// 拿不到结果也要发布（valid = false），编辑器不用等到超时
	auto publish = [&]() { _channel->PublishPick(std::move(result)); };

	auto idBuffer = ctx.GetInput(0);
	auto positionBuffer = ctx.GetInput(1);
	const glm::uvec2 size(state.framebuffer.width, state.framebuffer.height);
	if (!_shader.IsValid() || !idBuffer || idBuffer->IsEmpty() || !positionBuffer || positionBuffer->IsEmpty()
		|| size.x == 0 || size.y == 0 || request.targetSize != size
		|| idBuffer->GetWidth() != size.x || idBuffer->GetHeight() != size.y)
	{
		publish();
		return;
	}

	// 矩形截到画面范围内
	const glm::ivec2 maxPixel = glm::ivec2(size) - glm::ivec2(1);
	const glm::ivec2 rawLo = glm::min(request.rectMin, request.rectMax);
	const glm::ivec2 rawHi = glm::max(request.rectMin, request.rectMax);
	if (glm::any(glm::greaterThan(rawLo, maxPixel)) || glm::any(glm::lessThan(rawHi, glm::ivec2(0))))
	{
		publish();
		return;
	}
	const glm::ivec2 lo = glm::clamp(rawLo, glm::ivec2(0), maxPixel);
	const glm::ivec2 hi = glm::clamp(rawHi, glm::ivec2(0), maxPixel);
	const uint32_t width = uint32_t(hi.x - lo.x + 1);
	const uint32_t height = uint32_t(hi.y - lo.y + 1);
	const size_t count = size_t(width) * height;
	const bool wantPosition = request.wantPosition && !request.isBox;
	const size_t words = count * (wantPosition ? 4u : 1u);
	const vk::DeviceSize bytes = vk::DeviceSize(words) * sizeof(uint32_t);

	if (!_readback || _readback->GetSize() < bytes)
		_readback = std::make_shared<StorageBlock>(bytes);

	auto cmd = cmdCtx.GetCmd();
	if (!cmd)
	{
		publish();
		return;
	}

	// 两张输入图的布局由渲染图按 computeReadLayout 切好（gPosition 还有别的读者在并行，这里不能再改它的布局）
	PickParams params;
	params.rectMin = lo;
	params.rectSize = glm::ivec2(int(width), int(height));
	params.wantPosition = wantPosition ? 1u : 0u;

	ComputeBindingRecord binding;
	binding.SetUniformTexture(idBuffer, vk::ImageAspectFlagBits::eColor, 0);
	binding.SetUniformTexture(positionBuffer, vk::ImageAspectFlagBits::eColor, 1);
	binding.SetStorageBlock(_readback, 2);

	_shader.SetPushConstants(cmd, &params, sizeof(params));
	_shader.Bind(cmd, binding);
	cmd->dispatch(
		(width + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X,
		(height + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y,
		1);
	_readback->Barrier(cmd, BufferUsage::StorageWrite, BufferUsage::TransferRead);

	// 同步读回：Readback 里会提交并等待（和 DDGIApplyPass 的漏光调试一样）
	std::vector<uint32_t> data(words, 0u);
	const bool ok = _readback->Readback(cmd, data.data(), size_t(bytes));
	if (cmd->IsRecording())
		cmd->SubmitToQueue();
	if (!ok)
	{
		publish();
		return;
	}

	result.valid = true;
	// 缓冲里存的是 entityId + 1，0 = 没有物体
	auto decode = [](uint32_t v) { return v - 1u; };

	if (request.isBox)
	{
		std::vector<uint32_t> ids;
		for (size_t i = 0; i < count; ++i)
		{
			if (data[i] != 0u)
				ids.push_back(decode(data[i]));
		}
		std::sort(ids.begin(), ids.end());
		ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
		result.entities = std::move(ids);
	}
	else
	{
		// focus 处有物体就用它，没有就取矩形里离它最近的（细小物体也点得中）
		const glm::ivec2 focus = glm::clamp(request.focus, lo, hi) - lo;
		int64_t bestDist = std::numeric_limits<int64_t>::max();
		size_t bestIndex = count;
		for (uint32_t y = 0; y < height; ++y)
		{
			for (uint32_t x = 0; x < width; ++x)
			{
				const size_t index = size_t(y) * width + x;
				if (data[index] == 0u)
					continue;
				const int64_t dx = int64_t(x) - focus.x;
				const int64_t dy = int64_t(y) - focus.y;
				const int64_t dist = dx * dx + dy * dy;
				if (dist < bestDist)
				{
					bestDist = dist;
					bestIndex = index;
				}
			}
		}
		if (bestIndex < count)
		{
			result.focusEntity = decode(data[bestIndex]);
			if (wantPosition)
			{
				const size_t base = count + bestIndex * 3;
				const glm::vec3 position(std::bit_cast<float>(data[base]), std::bit_cast<float>(data[base + 1]), std::bit_cast<float>(data[base + 2]));
				if (std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z))
				{
					result.hasPosition = true;
					result.focusPosition = position;
				}
			}
		}
	}

	publish();
}

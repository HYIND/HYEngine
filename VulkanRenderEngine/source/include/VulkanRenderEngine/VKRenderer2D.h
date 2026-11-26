#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/General/VKRenderContext2D.h"
#include "VulkanRenderEngine/VKFramePipeline.h"

#include <mutex>

class Texture2D;
class GraphicsPipeline;
class StorageBlock;

// Renderer2D 的 Vulkan 后端：把一帧 Render2DFrame 叠到目标纹理上（保留目标原内容）。
// 所有四边形实例放进一个 SSBO，按 (layer, order, sequence) 稳定排序后切成批次：
//   - 同一批次混合方式相同；
//   - 每批次最多 kMaxTexturesPerBatch 张贴图，用可变长纹理数组绑定，实例里只存数组下标。
// 管线按目标格式懒创建，格式变化时重建。
//
// 它也是 VKFramePipeline 的一个阶段（接在 VKRenderer3D 后面）：每帧固定 1 段，把这一帧的 2D 内容叠到最终画面上。
// 同一时间只有一帧在跑这一段，所以实例缓冲、CPU 暂存这些各帧共用的东西不用额外加锁。
class VKRenderer2D : public IFrameStage
{
public:
	static constexpr uint32_t kMaxTexturesPerBatch = 256;

	// 挂在 FrameJob 上的 2D 输入：Render::Renderer 提交帧时带进来
	struct FrameInput : FrameStageData
	{
		std::shared_ptr<const VKRenderContext::Render2DFrame> frame;	// 和 3D 同一个逻辑帧的 2D 数据，提交后只读
		bool visible = true;											// 2D 层开关，提交时记下
	};

	explicit VKRenderer2D(std::string vertexShaderPath = "shader/Render2D/sprite2d.vs",
		std::string fragmentShaderPath = "shader/Render2D/sprite2d.fs");
	~VKRenderer2D() override;

	VKRenderer2D(const VKRenderer2D&) = delete;
	VKRenderer2D& operator=(const VKRenderer2D&) = delete;

	// 同步执行：录制、提交并等待完成后返回，返回后 target 处于可采样状态
	void Render(const std::shared_ptr<Texture2D>& target, const VKRenderContext::Render2DFrame& frame);

	void Release();

	// IFrameStage：每帧固定进出 1 段，没有 2D 内容或 2D 层关闭时是空段
	void Execute(FrameJob& job, uint32_t stageIndex) override;

private:
	struct GPUInstance
	{
		glm::vec2 origin;
		glm::vec2 axisX;
		glm::vec2 axisY;
		uint32_t texIndex;
		uint32_t flags;
		glm::vec4 uvRect;
		glm::vec4 color;
	};
	static_assert(sizeof(GPUInstance) == 64, "GPUInstance 必须和 sprite2d.vs 里的 std430 布局一致");

	struct PushConstants
	{
		glm::mat4 viewProj;
		glm::vec2 targetSize;
		uint32_t frameFlags;
		uint32_t padding;
	};

	struct Batch
	{
		VKRenderContext::BlendMode2D blend = VKRenderContext::BlendMode2D::Alpha;
		uint32_t firstInstance = 0;
		uint32_t instanceCount = 0;
		std::vector<std::shared_ptr<Texture2D>> textures;
	};

	bool EnsurePipelines(vk::Format format);
	void BuildBatches(const VKRenderContext::Render2DFrame& frame, const VKRenderContext::View2D& view);

private:
	std::string _vertexShaderPath;
	std::string _fragmentShaderPath;

	std::array<std::unique_ptr<GraphicsPipeline>, static_cast<size_t>(VKRenderContext::BlendMode2D::Count)> _pipelines;
	vk::Format _format = vk::Format::eUndefined;
	bool _pipelinesReady = false;

	std::shared_ptr<StorageBlock> _instanceBuffer;
	std::shared_ptr<Texture2D> _whiteTexture;

	// 每帧复用的 CPU 暂存
	std::vector<VKRenderContext::Sprite2DDrawItem> _lineItems;
	std::vector<const VKRenderContext::Sprite2DDrawItem*> _sorted;
	std::vector<GPUInstance> _instances;
	std::vector<Batch> _batches;

	std::mutex _mutex;
};

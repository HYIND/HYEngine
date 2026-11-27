#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/GraphicsPipeline.h"
#include "VulkanRenderEngine/General/RenderItem.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "RenderPassBase.h"

class GeometryPass :public RenderPassBase
{
public:
	GeometryPass(
		const std::string& staticMeshVertexShaderPath,
		const std::string& staticMeshFragmentShaderPath,
		const std::string& skinnedMeshVertexShaderPath = "shader/gbuffer/geometrypass_SkinnedMesh.vs"
	);
	~GeometryPass();

	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);
	virtual void FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state);

	// G-buffer 里两张按实际用到的通道数选的格式。建纹理（VKRenderer3D）和建管线（本类）必须用同一个值，所以统一从这里取。
	// 设备不支持时回退 RGBA16F。除了颜色附件 + 采样，还要求 storage：VmaImage 会给非 sRGB 的颜色格式自动加 storage 用途，
	// 格式不支持 storage 时建纹理本身就会失败。结果第一次查询后缓存（设备不变）。
	//   运动矢量：只用 .xy → R16G16_SFLOAT（每通道精度与 RGBA16F 相同，无损）
	//   自发光：只用 .rgb，非负 → B10G11R11_UFLOAT（6/6/5 位尾数，最大约 65000）
	static vk::Format GetMotionVectorFormat();
	static vk::Format GetEmissionFormat();

	// 编辑器的实体 ID 缓冲（GlobalConfig::EditorViewportFeatures 打开时是第 7 个颜色附件，Output 6）：
	// 每像素存 entityId + 1，0 = 没有物体（清屏值）。不透明的静态 / 蒙皮网格在 G-Buffer 里一起写，镂空（opacity < 0.01）的像素被 discard；
	// 透明网格随后单独写一遍（见 TransparentPickOpacity），这时渲染图要给 Temp 0（和 gDepthStencil 同格式的临时深度）。
	// 整数格式，附件的混合必须关掉
	static vk::Format GetEntityIdFormat() { return vk::Format::eR32Uint; }
	static bool HasEntityIdOutput();
	// 透明物体（AlphaMode::Blend）也写进实体 ID 缓冲：不透明物体画完之后，用不透明深度的一份拷贝（Temp 0）做深度测试并写入，
	// 鼠标下最近的透明面覆盖后面物体的 ID。材质透明度低于这个值的地方不算物体（点得穿）。
	// 选中遮罩（EditorSelectionMaskPass）画透明物体时用同一个阈值，描边和拾取范围一致
	static constexpr float TransparentPickOpacity = 0.02f;

private:
	DynamicRenderInfo GenerateDynamicRenderInfo(
		RenderState& state,
		std::shared_ptr<Texture2D>& gPosition,
		std::shared_ptr<Texture2D>& gNormal,
		std::shared_ptr<Texture2D>& gAlbedoOpacity,
		std::shared_ptr<Texture2D>& gMetallicRoughnessMap,
		std::shared_ptr<Texture2D>& gMotionVectorMap,
		std::shared_ptr<Texture2D>& gEmission,
		std::shared_ptr<Texture2D>& tempDepthStencilMap,
		const std::shared_ptr<Texture2D>& gEntityId		// 可以为空（没开编辑器视口功能）
	);

	bool SetupStaticBufferData(
		const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		GraphicsBindingRecord& binding,
		std::vector<VKRenderObjectData::SceneStaticRenderData::MeshItem>& items,
		VKRenderObjectData::RenderIndex& renderIndex,
		std::vector<IndirectDrawCommand>& oneSideCommands,
		std::vector<IndirectDrawCommand>& twoSideCommands
	);
	void RenderSceneGeometryPassStatic(RenderGraph::FrameDataRegistry& registry, const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state, DynamicRenderInfo& renderInfo, DynamicViewport& viewPort);
	// 编辑器：透明物体写实体 ID（见 TransparentPickOpacity）。tempDepth：和 gDepthStencil 同格式的临时深度
	void RenderTransparentEntityIds(RenderGraph::FrameDataRegistry& registry, const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderState& state,
		const std::shared_ptr<Texture2D>& gEntityId, const std::shared_ptr<Texture2D>& gDepthStencil, const std::shared_ptr<Texture2D>& tempDepth);

private:
	std::shared_ptr<GraphicsPipeline> _staticShader;
	// 蒙皮网格用另一套 VS：除了静态那套，还要从上一帧的蒙皮顶点缓冲取重投影位置
	std::shared_ptr<GraphicsPipeline> _skinnedShader;
	// 没有上一帧顶点缓冲时占位用的假缓冲（重投影关闭时 shader 不会读它）
	std::shared_ptr<VertexBufferBlock> _dummySkinnedVertexBuffer;
	// 构造时按 GlobalConfig::EditorViewportFeatures 决定的：两条管线是否带实体 ID 附件（之后不变）
	bool _hasEntityId = false;
	// 编辑器：透明物体写实体 ID 的两条管线（只有一个 R32_UINT 附件 + 深度），_hasEntityId 时才创建
	std::shared_ptr<GraphicsPipeline> _transparentIdStaticShader;
	std::shared_ptr<GraphicsPipeline> _transparentIdSkinnedShader;
};

// G-Buffer 历史的收尾拷贝：把本帧的 gPosition / gNormal / gDepthStencil 拷进三张历史纹理（Persistent），给下一帧的时域累积用。
// 读历史的 Pass（RTR / RTGI / SSR / SSGI / VolumeLut）直接把历史纹理声明成 Persistent，在本 Pass 之前读到的就是上一帧的内容。
// 渲染图不按 Persistent 推导依赖，执行顺序由 VKRenderer3D 里的节点声明手动指定。
//   Input 0 ~ 1：gPosition / gNormal；FrameLocal 0：gDepthStencil；Persistent 0 ~ 2：对应的三张历史
class GBufferHistoryCopyPass :public RenderPassBase
{
public:
	GBufferHistoryCopyPass() = default;

	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state) override;
};

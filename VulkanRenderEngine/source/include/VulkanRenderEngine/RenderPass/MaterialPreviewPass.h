#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/Material.h"
#include "VulkanRenderEngine/Base/Texture2D.h"

#include <array>
#include <map>
#include <memory>
#include <mutex>

class GraphicsPipeline;
class VertexBufferBlock;
class IndexBufferBlock;
class HDRIEnvironment;

// 材质预览的输入：属性 + 各槽贴图（没有的槽不放）。
// 贴图必须带采样器（不能用 createSampler = false 建的编辑器缩略图贴图），格式按资产配置选 sRGB / UNORM
struct MaterialPreviewInput
{
	MaterialProperties properties;
	std::map<TextureType, std::shared_ptr<Texture2D>> textures;
};

// 预览用的网格，都放在原点、包围半径约 1
enum class MaterialPreviewShape : uint32_t
{
	Sphere = 0,		// 单位 UV 球
	Cube,			// 每个面一套完整 0~1 UV
	Plane,			// 竖直放置、正面朝 +Z 的正方形（双面），看贴图本身最直观
	Count
};

// 角度单位：度。方向约定（相机和灯光一致）：yaw 绕 Y 轴，0 = 位于 +Z 一侧；pitch 是高度角，正值 = 在上方
struct MaterialPreviewView
{
	MaterialPreviewShape shape = MaterialPreviewShape::Sphere;

	// 相机：围绕原点
	float yaw = -25.0f;
	float pitch = 18.0f;
	float distance = 4.3f;
	float fovY = 30.0f;

	// 主光方向（从物体指向光源）。lightFollowCamera = true 时 lightYaw 是相对相机 yaw 的偏移（转视角时灯跟着转）。
	// 轮廓光自动放在主光的对侧后上方
	float lightYaw = -40.0f;
	float lightPitch = 41.0f;
	bool lightFollowCamera = false;

	float exposure = 1.0f;
	uint32_t supersample = 2;	// 内部按输出尺寸的几倍渲染，再线性缩小到输出（抗锯齿），1~4；内部尺寸最大 2048

	// ---- HDRI 环境 ----
	// 为空：摄影棚灯光（主光 + 轮廓光 + 解析式环境光），背景透明（alpha = 0，调用方自己画棋盘格）。缩略图一律用这个。
	// 不为空：IBL 照明（漫反射辐照度 + 镜面预过滤 + BRDF LUT，和场景 IBL 同一套公式），HDRI 画成背景，输出不透明；
	//   Blend 材质按透明度直接和背后的 HDRI 混合
	std::shared_ptr<const HDRIEnvironment> environment;
	float environmentYaw = 0.0f;			// 环境绕 Y 轴旋转（度）
	float environmentIntensity = 1.0f;		// 环境亮度倍数（照明和背景一起）
	float backgroundBlur = 0.0f;			// 背景模糊 0~1（按背景 cube 的 mip 取），只影响背景，不影响照明
	bool environmentDirectLights = false;	// HDRI 下是否再叠加主光 + 轮廓光
};

// 独立的前向渲染 Pass：不进 RenderGraph、不读 RenderState、不用 IndirectDrawManager / 全局 bindless 表 / 场景光照。
// 自带管线、三种预览网格和固定的摄影棚光照（两盏方向光 + 解析式环境光）或 HDRI 环境（IBL + 背景，见 MaterialPreviewView::environment），Render() 同步画一帧到调用方给的 output
// （RGBA8 UNORM，画完是可采样布局）。
//   - 线程：只读的共享资源（管线、网格、占位图）第一次用时创建，之后不再修改；每次 Render 用到的材质 SSBO、
//     中间附件、绑定记录、命令缓冲都是局部的，可以在多个线程上同时调用（编辑器缩略图在加载器线程上调用）。
//   - 生命周期：第一次 Render 时创建并登记 VKContext 的 shutdown 回调；设备销毁前释放，之后 Render 一律返回 false。
//   - 不读回 CPU、不写任何文件。
class MaterialPreviewPass
{
public:
	static MaterialPreviewPass& Instance();

	bool Render(const MaterialPreviewInput& input, const MaterialPreviewView& view, const std::shared_ptr<Texture2D>& output);

	// 设备销毁前（VKContext::Shutdown 回调）：释放管线和网格
	void Release();

private:
	MaterialPreviewPass() = default;
	~MaterialPreviewPass() = default;
	MaterialPreviewPass(const MaterialPreviewPass&) = delete;
	MaterialPreviewPass& operator=(const MaterialPreviewPass&) = delete;

	struct MeshBuffers
	{
		std::shared_ptr<VertexBufferBlock> vertices;
		std::shared_ptr<IndexBufferBlock> indices;
		uint32_t indexCount = 0;
	};

	struct Resources
	{
		std::shared_ptr<GraphicsPipeline> pipeline;			// 摄影棚灯光
		std::shared_ptr<GraphicsPipeline> iblPipeline;		// HDRI 照明（同一个着色器，定义 PREVIEW_IBL）；创建失败时为空，退回摄影棚
		std::shared_ptr<GraphicsPipeline> skyPipeline;		// HDRI 背景（全屏三角形）
		std::array<MeshBuffers, static_cast<size_t>(MaterialPreviewShape::Count)> meshes;
		MeshBuffers fullscreenTriangle;
		std::shared_ptr<Texture2D> placeholder;		// 1×1 白图：一张贴图都没有时也要给贴图数组绑点东西
	};

	std::shared_ptr<const Resources> Acquire();
	static std::shared_ptr<Resources> CreateResources();

private:
	std::mutex _mutex;
	std::shared_ptr<const Resources> _resources;
	bool _initFailed = false;		// 创建失败（例如着色器编译不过）后不再每次重试
	bool _released = false;			// 设备已经销毁
	bool _shutdownRegistered = false;
};

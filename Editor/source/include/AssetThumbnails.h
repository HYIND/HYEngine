#pragma once

#include "Project/AssetMeta.h"
#include "VulkanRenderEngine/Base/Sprite2D.h"

class ProjectManager;
class Material;

// 资产缩略图：编辑器里的独立模块，和资产系统完全分开，可以整个关掉（关掉后资产浏览器显示类型图标）。
//   - 只读元文件拿原图路径和过滤方式：不调 LoadAsset、不进资产表、不碰渲染器正在用的贴图。
//   - 后台任务借 AssetLoader 的线程（Thumbnail 通道，同时最多 3 个、最低优先级）：
//     CPU 解码 -> 缩到最长边 128 -> 原图像素马上丢掉 -> 上传成只给编辑器用的小贴图（上传完就切成采样布局）。
//     动图逐帧解码、逐帧缩小，拼成小图集，整个缩略图不超过约 4MB（超出时每帧缩到 96 / 64 / 48 / 32，再不够就抽帧）。
//   - 缩略图贴图不建采样器（ImGui 用它后端自己的采样器），整个模块不占 VkSampler。
//   - 结果在这里缓存，按最近使用淘汰：个数不超过 3000、总字节数不超过 256MB（累计值，接收结果时加、释放时减）；
//     淘汰时同时从编辑器的 ImGui 贴图缓存里移除。只有调用方真正画出来的（在视野里的）才会请求。
//   - 元文件或原图改了会自动重新生成（只检查最近在画的，约 2 秒一次）；编辑器自己改了资产调 Invalidate 立刻刷新。
//     重新生成期间继续显示旧的。
//   - 材质：后台任务读材质元文件，依赖贴图从原图独立解码、缩到 256 以内（不走 LoadAsset、不进资产表、用完就释放），
//     交给 MaterialPreviewPass 渲染成 128×128 的材质球；内存里只留渲染结果，不读回 CPU、不写磁盘。
//     材质元文件、依赖贴图的元文件或原图改了都会重新渲染。
//
// 所有接口只在编辑器主线程（ImGui 线程）调用。
namespace AssetThumbnails
{
	// 贴图 / 精灵 / 动图 / 材质
	bool IsSupported(AssetType type);

	// 关掉时清空缓存、取消排队的任务；之后 Get 一律返回无效帧
	void SetEnabled(bool enabled);
	bool IsEnabled();

	// 每帧调一次（ImGui::NewFrame 之后）：检测工程切换、取消长时间没画的排队任务、淘汰
	void BeginFrame(ProjectManager* projectManager);

	// 取要画的一帧（动图按 timeMs 循环播放）。还没生成好 / 生成失败 / 不支持 / 已关掉时返回无效帧，
	// 调用方画类型图标。需要时会提交后台任务，不会阻塞
	Render2D::SpriteFrame Get(ProjectManager* projectManager, const AssetMeta& meta, float timeMs);

	// 在当前光标处占一个 side × side 的方格画缩略图（不在视野里就只占位、不请求）；没有缩略图时画类型图标，
	// guid 为空时画空格子。悬停时显示大图和资产名。属性面板 / 材质编辑器的贴图槽用
	void DrawThumbnailBox(ProjectManager* projectManager, const AssetGUID& guid, float side);

	// 模型组件材质槽的材质球。
	//   有资产的材质：和资产浏览器共用同一份（按 GUID、按磁盘上的元文件），两边谁先看过另一边就直接有。
	//   实例材质（模型内嵌 / 覆盖，没有资产）：按运行时材质对象的当前状态渲染，贴图直接用已经加载好的只读贴图；
	//     按材质对象缓存，材质版本或贴图变了自动重画（重画期间继续显示旧的），材质对象释放了或很久没画就丢掉。
	// 都只在内存里。
	// 还没生成好时返回无效帧
	Render2D::SpriteFrame GetMaterial(ProjectManager* projectManager, const std::shared_ptr<Material>& material);

	// 在当前光标处占一个 side × side 的按钮画运行时材质球（不在视野里只占位、不请求）；悬停显示大图和 label。
	// 返回是否双击了。按钮是最后一个 item，调用方可以接着做拖放目标
	bool DrawMaterialBox(ProjectManager* projectManager, const std::shared_ptr<Material>& material, float side, const char* label = nullptr);

	// 资产改了（导入配置、重新导入）：下次画到时重新生成，生成好之前继续显示旧的
	void Invalidate(const AssetGUID& guid);

	// 清空缓存（释放全部缩略图贴图，取消排队的任务）
	void Clear();

	// 关闭编辑器时调用，要在等 GPU 空闲、关 ImGui 后端之前：取消排队的任务，等正在执行的做完，释放全部贴图
	void Shutdown();
}

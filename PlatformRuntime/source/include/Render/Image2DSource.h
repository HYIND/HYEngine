#pragma once

#include "VulkanRenderEngine/Base/Sprite2D.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

// 2D 层和 UI 层共用的"图像来源"：一个图像引用解析出来的 Sprite 或 AnimatedImage。
// 引用只认资产体系（不直接读图片文件），字符串里只有资产本身，不带区域：
//   "<资产路径>"   资产库里登记的路径，例如 "UI/Sprite_1_btn.png.meta"（编辑器右键"复制资产路径"）
//   "<guid>"       编辑器右键"复制 GUID"
// 先按资产路径找，再按 GUID 找；找到的资产类型必须是 Sprite / Texture / AnimatedImage 并且能加载出贴图 / 帧，
// 否则视为没找到、继续试下一种（都不行就解析失败，控制台有提示）。
// 区域（Sprite 里的命名区域）在 .ui 里是单独的 "region" 字段，取帧时传给 Evaluate
namespace Render2D
{
	struct ImageSource
	{
		std::shared_ptr<Sprite> sprite;
		std::shared_ptr<AnimatedImage> animated;

		bool IsValid() const { return sprite || animated; }
		bool IsAnimated() const { return static_cast<bool>(animated); }

		// 动图的循环次数、速度由使用方给（组件 / UI 节点），资源本身不带播放方式。
		// region：Sprite 的区域名；为空或找不到就用整张图（动图没有区域，忽略）
		SpriteFrame Evaluate(float timeMs = 0.0f, int loopCount = 0, float speed = 1.0f, const std::string& region = {}) const
		{
			if (animated)
				return animated->Evaluate(timeMs * speed, loopCount);
			if (sprite)
				return sprite->Resolve(region);	// 为空或找不到 = 整张图
			return {};
		}
	};

	using ImageResolver = std::function<ImageSource(const std::string& reference)>;

	// 一份 UI 文档里所有图像引用解析后的结果（键是文档里 image 字段写的原始字符串）
	using UIImageTable = std::unordered_map<std::string, ImageSource>;
}

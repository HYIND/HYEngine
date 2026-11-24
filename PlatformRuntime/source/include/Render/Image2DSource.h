#pragma once

#include "VulkanRenderEngine/Base/Sprite2D.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

// 2D 层和 UI 层共用的"图像来源"：一个引用字符串解析出来的 Sprite（+ 区域）或 AnimatedImage。
// 引用格式：
//   "<guid>"                     Sprite / Texture / AnimatedImage 资产
//   "<guid>#<region>"            Sprite 里的某个区域
//   "Assets/ui/btn.png"          工程内文件路径（找得到对应资产就用资产，否则直接按图片文件加载）
//   "Assets/ui/btn.png#idle"     同上，带区域
namespace Render2D
{
	struct ImageSource
	{
		std::shared_ptr<Sprite> sprite;
		std::string region;
		std::shared_ptr<AnimatedImage> animated;

		bool IsValid() const { return sprite || animated; }
		bool IsAnimated() const { return static_cast<bool>(animated); }

		// 动图的循环次数、速度由使用方给（组件 / UI 节点），资源本身不带播放方式
		SpriteFrame Evaluate(float timeMs = 0.0f, int loopCount = 0, float speed = 1.0f) const
		{
			if (animated)
				return animated->Evaluate(timeMs * speed, loopCount);
			if (sprite)
				return sprite->Resolve(region);
			return {};
		}
	};

	using ImageResolver = std::function<ImageSource(const std::string& reference)>;

	inline void SplitImageReference(const std::string& reference, std::string& asset, std::string& region)
	{
		const auto hash = reference.find('#');
		if (hash == std::string::npos)
		{
			asset = reference;
			region.clear();
			return;
		}
		asset = reference.substr(0, hash);
		region = reference.substr(hash + 1);
	}

	// 一份 UI 文档里所有图像引用解析后的结果（键是文档里写的原始引用字符串）
	using UIImageTable = std::unordered_map<std::string, ImageSource>;
}

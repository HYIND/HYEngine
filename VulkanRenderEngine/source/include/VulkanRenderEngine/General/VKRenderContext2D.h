#pragma once

#include "vkstdafx.h"

#include <climits>

class Texture2D;

// 2D 层一帧要画的东西。和 3D 的 VKRenderContext::RenderContext 不同，这里全是平铺的值类型数组：
// 2D 对象数量多，每个都 make_shared + 虚分发不划算；场景线程收集、渲染线程一次性批处理。
//
// 坐标约定：2D 世界坐标 y 朝下（和屏幕、UI 一致），角度单位是度，正方向为顺时针。
namespace VKRenderContext
{
	enum class BlendMode2D : uint32_t
	{
		Alpha = 0,		// 普通透明混合
		Additive,		// 叠加（发光、粒子）
		Multiply,		// 正片叠底（阴影、暗角）
		Premultiplied,	// 贴图已预乘 alpha
		Count
	};

	// 一个四边形：corner(u, v) = origin + axisX * u + axisY * v，u, v ∈ [0, 1]；(0,0) 对应 uvRect.xy
	// pivot、尺寸、缩放、翻转、旋转都已经折算进 origin / axisX / axisY
	struct Sprite2DDrawItem
	{
		std::shared_ptr<Texture2D> texture;		// 为空表示纯色块
		glm::vec2 origin = glm::vec2(0.0f);
		glm::vec2 axisX = glm::vec2(1.0f, 0.0f);
		glm::vec2 axisY = glm::vec2(0.0f, 1.0f);
		glm::vec4 uvRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
		glm::vec4 color = glm::vec4(1.0f);
		BlendMode2D blend = BlendMode2D::Alpha;
		int layer = 0;
		int order = 0;
		uint32_t sequence = 0;		// layer / order 相同时按它排（一般填实体 id），保证每帧顺序稳定、不闪
	};

	// 调试线：宽度按屏幕像素算，不随相机缩放变粗变细
	struct Line2DDrawItem
	{
		glm::vec2 from = glm::vec2(0.0f);
		glm::vec2 to = glm::vec2(0.0f);
		glm::vec4 color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
		float width = 1.0f;
		int layer = INT_MAX;		// 默认画在所有精灵之上
	};

	enum class AspectMode2D : uint32_t
	{
		Stretch = 0,	// 两轴各自拉伸，正好显示设计区域（变形）
		Fit,			// 等比缩放并完整显示设计区域，宽高比不同时多看到一些区域
		Fill,			// 等比缩放并铺满屏幕，宽高比不同时裁掉一部分设计区域
	};

	struct Camera2DData
	{
		// 没有 Camera2D 组件时的默认值：等同旧的 MapBoundary（[0,1000] x [0,600] 拉伸到整个目标）
		glm::vec2 center = glm::vec2(500.0f, 300.0f);
		glm::vec2 designSize = glm::vec2(1000.0f, 600.0f);
		float zoom = 1.0f;
		float rotation = 0.0f;
		AspectMode2D aspectMode = AspectMode2D::Stretch;
		bool pixelSnap = false;		// 顶点吸附到整像素（像素风）
	};

	struct Render2DFrame
	{
		std::vector<Sprite2DDrawItem> sprites;
		std::vector<Line2DDrawItem> lines;
		Camera2DData camera;
		bool hasContent = false;

		void reset()
		{
			sprites.clear();
			lines.clear();
			camera = Camera2DData{};
			hasContent = false;
		}
	};

	// 相机 + 目标尺寸 => 实际可见的世界区域与变换
	struct View2D
	{
		glm::mat4 viewProj = glm::mat4(1.0f);		// 世界 -> 裁剪空间（Vulkan，y 朝下）
		glm::vec2 center = glm::vec2(0.0f);
		glm::vec2 visibleSize = glm::vec2(1.0f);	// 可见的世界尺寸
		glm::vec2 targetSize = glm::vec2(1.0f);		// 像素
		float rotation = 0.0f;						// 度

		glm::vec2 PixelsPerUnit() const { return targetSize / glm::max(visibleSize, glm::vec2(1e-6f)); }

		// 目标像素坐标（左上角为原点）<-> 世界坐标
		glm::vec2 ScreenToWorld(const glm::vec2& pixel) const
		{
			const glm::vec2 ndc = pixel / glm::max(targetSize, glm::vec2(1.0f)) * 2.0f - 1.0f;
			const glm::vec2 local = ndc * visibleSize * 0.5f;
			const float r = glm::radians(rotation);
			const float c = std::cos(r), s = std::sin(r);
			return center + glm::vec2(c * local.x - s * local.y, s * local.x + c * local.y);
		}

		glm::vec2 WorldToScreen(const glm::vec2& world) const
		{
			const glm::vec2 d = world - center;
			const float r = glm::radians(rotation);
			const float c = std::cos(r), s = std::sin(r);
			const glm::vec2 local(c * d.x + s * d.y, -s * d.x + c * d.y);
			const glm::vec2 ndc = local / (visibleSize * 0.5f);
			return (ndc * 0.5f + 0.5f) * targetSize;
		}
	};

	inline View2D ComputeView2D(const Camera2DData& camera, uint32_t targetWidth, uint32_t targetHeight)
	{
		View2D view;
		const glm::vec2 target(static_cast<float>(std::max(1u, targetWidth)), static_cast<float>(std::max(1u, targetHeight)));
		const glm::vec2 design = glm::max(camera.designSize, glm::vec2(1.0f));

		glm::vec2 visible = design;
		switch (camera.aspectMode)
		{
		case AspectMode2D::Fit:
		{
			const float scale = std::min(target.x / design.x, target.y / design.y);
			visible = target / scale;
			break;
		}
		case AspectMode2D::Fill:
		{
			const float scale = std::max(target.x / design.x, target.y / design.y);
			visible = target / scale;
			break;
		}
		default:
			break;
		}
		visible /= std::max(camera.zoom, 1e-4f);

		view.center = camera.center;
		view.visibleSize = visible;
		view.targetSize = target;
		view.rotation = camera.rotation;

		// p' = R(-θ)(p - center)，再按可见区域缩放到 [-1, 1]
		const float r = glm::radians(camera.rotation);
		const float c = std::cos(r), s = std::sin(r);
		const float sx = 2.0f / visible.x;
		const float sy = 2.0f / visible.y;

		glm::mat4 m(1.0f);
		m[0] = glm::vec4(c * sx, -s * sy, 0.0f, 0.0f);
		m[1] = glm::vec4(s * sx, c * sy, 0.0f, 0.0f);
		m[2] = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
		m[3] = glm::vec4(-(c * camera.center.x + s * camera.center.y) * sx, -(-s * camera.center.x + c * camera.center.y) * sy, 0.0f, 1.0f);
		view.viewProj = m;
		return view;
	}
}

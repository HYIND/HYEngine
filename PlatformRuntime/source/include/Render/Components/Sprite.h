#pragma once

#include "RenderBase.h"
#include "ECSCore/IComponent.h"
#include "CommonData/AssetGUIDDef.h"
#include "VulkanRenderEngine/Base/Sprite2D.h"
#include "VulkanRenderEngine/General/VKRenderContext2D.h"

#include <memory>
#include <string>

enum class SpriteDrawMode : uint32_t
{
	Simple = 0,		// 整个区域拉伸到 size
	Sliced,			// 九宫格：边距按像素保持不变，只拉伸中间
};

// 2D 精灵。和 Transform2D 一起用：位置 / 旋转 / 缩放取 Transform2D，渲染中心再加 RenderBase2D::offset。
// 资产引用 spriteGuid 可以是 Sprite 资产，也可以直接是 Texture 资产（整张贴图）。
struct Sprite : public RenderBase2D
{
	AssetGUID spriteGuid;
	std::string region;							// Sprite 里的区域名；为空用默认（第一个）区域

	glm::vec2 size = glm::vec2(0.0f);			// 世界尺寸；分量 <= 0 时按区域像素尺寸（1 像素 = 1 单位）
	bool overridePivot = false;					// false：用资产区域里的轴心
	glm::vec2 pivot = glm::vec2(0.5f);
	glm::vec4 color = glm::vec4(1.0f);			// 染色（含透明度）
	bool flipX = false;
	bool flipY = false;
	SpriteDrawMode drawMode = SpriteDrawMode::Simple;
	VKRenderContext::BlendMode2D blend = VKRenderContext::BlendMode2D::Alpha;

	// ---- 运行时（不序列化）：由 Render2DResourceSystem 按 spriteGuid 解析 ----
	std::shared_ptr<Render2D::Sprite> resource;
	AssetGUID resolvedGuid;
	bool resolveFailed = false;

	void OnRemove(const Entity& e) override
	{
		resource.reset();
		resolvedGuid.clear();
		resolveFailed = false;
	}

	bool NeedResolve() const { return resolvedGuid != spriteGuid; }
	Render2D::SpriteFrame ResolveFrame() const { return resource ? resource->Resolve(region) : Render2D::SpriteFrame{}; }
};

HY_DECLARE_ECS_TYPE(Sprite, "hy.engine.component.Sprite");

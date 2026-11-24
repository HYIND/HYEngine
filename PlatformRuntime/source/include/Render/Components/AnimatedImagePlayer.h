#pragma once

#include "RenderBase.h"
#include "ECSCore/IComponent.h"
#include "CommonData/AssetGUIDDef.h"
#include "VulkanRenderEngine/Base/Sprite2D.h"
#include "VulkanRenderEngine/General/VKRenderContext2D.h"

#include <memory>

// 播放一个 AnimatedImage 资产。资产只描述"有哪些帧、每帧多长"，怎么播全由这个组件决定。
// 时间按游戏时间推进：World 暂停 / 变速时跟着停 / 变速。
struct AnimatedImagePlayer : public RenderBase2D
{
	AssetGUID imageGuid;

	// ---- 播放 ----
	bool playing = true;
	int loopCount = 0;					// <= 0 无限循环；> 0 播完停在最后一帧
	float speed = 1.0f;					// 倍速，可以为 0
	float time = 0.0f;					// 当前播放时间（毫秒）
	bool restartRequested = false;		// 置 true 后下一帧从头播

	// ---- 外观（和 Sprite 一致） ----
	glm::vec2 size = glm::vec2(0.0f);	// 世界尺寸；分量 <= 0 时按帧像素尺寸
	glm::vec2 pivot = glm::vec2(0.5f);
	glm::vec4 color = glm::vec4(1.0f);
	bool flipX = false;
	bool flipY = false;
	VKRenderContext::BlendMode2D blend = VKRenderContext::BlendMode2D::Alpha;

	// ---- 运行时（不序列化） ----
	std::shared_ptr<Render2D::AnimatedImage> resource;
	AssetGUID resolvedGuid;
	bool resolveFailed = false;
	bool finished = false;

	void OnRemove(const Entity& e) override
	{
		resource.reset();
		resolvedGuid.clear();
		resolveFailed = false;
		finished = false;
	}

	bool NeedResolve() const { return resolvedGuid != imageGuid || (resource && resource->IsStale()); }

	Render2D::SpriteFrame ResolveFrame() const
	{
		return resource ? resource->Evaluate(time, loopCount, pivot) : Render2D::SpriteFrame{};
	}
};

HY_DECLARE_ECS_TYPE(AnimatedImagePlayer, "hy.engine.component.AnimatedImagePlayer");

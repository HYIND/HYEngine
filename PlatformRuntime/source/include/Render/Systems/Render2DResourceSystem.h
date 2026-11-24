#pragma once

#include "ECSCore/System.h"
#include "ECSCore/IComponent.h"
#include "Render/Image2DSource.h"

#include <memory>

namespace UI { struct ParseResult; }

// UI 文档里引用到的图像解析结果（运行时组件，挂在带 UIViewRuntime 的实体上）
struct UIViewImageTable : public IComponent
{
	std::shared_ptr<UI::ParseResult> document;				// 表是按哪一份文档建的；文档热重载后重建
	std::shared_ptr<const Render2D::UIImageTable> table;

	void OnRemove(const Entity& e) override
	{
		document.reset();
		table.reset();
	}

	bool ShouldDuplicate() const override { return false; }
};

HY_DECLARE_ECS_TYPE(UIViewImageTable, "hy.engine.component.UIViewImageTable");

// 2D 资源桥：把组件 / UI 文档里的资产引用解析成运行时资源，并推进动图的播放时间。
//   update     ：只在 World 运行时调用，按游戏时间推进 AnimatedImagePlayer
//   postUpdate ：解析 Sprite / AnimatedImagePlayer / UI 图像引用；编辑态（World 不运行）也由 EngineRuntime 调用，
//                所以编辑器里拖进来的精灵马上能看到
class Render2DResourceSystem : public System
{
public:
	Render2DResourceSystem();

	void SetImageResolver(Render2D::ImageResolver resolver) { _resolver = std::move(resolver); }
	bool HasImageResolver() const { return static_cast<bool>(_resolver); }

	void update(float deltaTime) override;
	void postUpdate(float deltaTime) override;

private:
	void ResolveSprites();
	void ResolveAnimatedImages();
	void ResolveUIImages();

	Render2D::ImageSource Resolve(const std::string& reference) const;

private:
	Render2D::ImageResolver _resolver;
};

HY_DECLARE_ECS_TYPE(Render2DResourceSystem, "hy.engine.system.Render2DResource");

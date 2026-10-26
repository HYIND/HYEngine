#pragma once

#include "ECSCore/System.h"

#include <functional>
#include <memory>
#include <string>

namespace UI { struct ParseResult; }

// UIView（场景里声明的 UI）和 UIViewRuntime（加载好的文档）之间的桥。
class UIViewSystem : public System
{
public:
	using DocumentLoader = std::function<std::shared_ptr<UI::ParseResult>(const std::string& guid)>;

	UIViewSystem();
	~UIViewSystem() override;

	void onAttach(World& world) override;
	void onDetach() override;
	void update(float deltaTime) override;

	// 注入文档加载方式；没注入时所有视图都会被标记为加载失败。
	void SetDocumentLoader(DocumentLoader loader);
	bool HasDocumentLoader() const { return _loader != nullptr; }

private:
	DocumentLoader _loader;
};

HY_DECLARE_ECS_TYPE(UIViewSystem, "hy.engine.system.UIView");

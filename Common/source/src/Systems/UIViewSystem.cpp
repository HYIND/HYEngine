#include "Systems/UIViewSystem.h"

#include "Components/UIView.h"
#include "ECSCore/World.h"

#include <utility>

UIViewSystem::UIViewSystem() : System("UIViewSystem")
{}

UIViewSystem::~UIViewSystem() = default;

void UIViewSystem::onAttach(World& world)
{}

void UIViewSystem::onDetach()
{
	_loader = nullptr;
}

void UIViewSystem::SetDocumentLoader(DocumentLoader loader)
{
	_loader = std::move(loader);
}

static std::shared_ptr<UI::ParseResult> LoadDocument(AssetGUID guid, UIViewSystem::DocumentLoader& loader)
{
	if (guid.empty() || !loader)
		return nullptr;

	auto document = loader(guid);
	if (!document || !document->ok)
		return nullptr;

	return document;
}

void UIViewSystem::update(float deltaTime)
{
	auto& world = getWorld();
	auto views = world.getViewWith<UIView>();
	for (auto [entity, uiView] : views)
	{
		bool needReload = uiView.needReload;
		if (needReload)
		{
			bool hasComponent = entity.hasComponent<UIViewRuntime>();
			auto& runtime = hasComponent ? entity.getComponent<UIViewRuntime>() : entity.addComponent<UIViewRuntime>();

			if (hasComponent)
			{
				runtime.document.reset();
				runtime.instance.reset();
				runtime.faulted = false;
			}

			if (auto document = LoadDocument(uiView.documentGuid, _loader))
			{
				runtime.document = std::move(document);
				runtime.instance = std::make_shared<UI::UiInstanceState>();
				UI::SeedInstanceState(runtime.document->document, *runtime.instance);
			}
			else
			{
				runtime.faulted = true;
			}

			uiView.needReload = false;
		}
	}
}

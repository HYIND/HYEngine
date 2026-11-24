#include "Render/Systems/Render2DResourceSystem.h"

#include "ECSCore/World.h"
#include "Components/UIView.h"
#include "Render/Components/Sprite.h"
#include "Render/Components/AnimatedImagePlayer.h"

#include <unordered_set>

Render2DResourceSystem::Render2DResourceSystem()
	: System("Render2DResource")
{}

Render2D::ImageSource Render2DResourceSystem::Resolve(const std::string& reference) const
{
	if (reference.empty() || !_resolver)
		return {};
	return _resolver(reference);
}

void Render2DResourceSystem::update(float deltaTime)
{
	if (!m_world)
		return;

	auto players = m_world->getViewWith<AnimatedImagePlayer>();
	for (auto [entity, player] : players)
	{
		if (player.restartRequested)
		{
			player.time = 0.0f;
			player.finished = false;
			player.restartRequested = false;
		}

		if (!player.playing || !player.resource || player.finished)
			continue;

		player.time += deltaTime * player.speed;
		if (player.time < 0.0f)
			player.time = 0.0f;

		if (player.resource->IsFinished(player.time, player.loopCount))
		{
			player.finished = true;
			player.playing = false;
		}
		// 无限循环时把时间收回到一个周期内，避免浮点数越加越大丢精度
		else if (player.loopCount <= 0 && player.resource->GetTotalDuration() > 0.0f)
		{
			player.time = std::fmod(player.time, player.resource->GetTotalDuration());
		}
	}
}

void Render2DResourceSystem::postUpdate(float deltaTime)
{
	if (!m_world)
		return;

	ResolveSprites();
	ResolveAnimatedImages();
	ResolveUIImages();
}

void Render2DResourceSystem::ResolveSprites()
{
	auto sprites = m_world->getViewWith<Sprite>();
	for (auto [entity, sprite] : sprites)
	{
		if (!sprite.NeedResolve())
			continue;

		sprite.resolvedGuid = sprite.spriteGuid;
		sprite.resource.reset();
		sprite.resolveFailed = false;
		if (sprite.spriteGuid.empty())
			continue;

		auto source = Resolve(sprite.spriteGuid);
		sprite.resource = source.sprite;
		sprite.resolveFailed = !sprite.resource;
	}
}

void Render2DResourceSystem::ResolveAnimatedImages()
{
	auto players = m_world->getViewWith<AnimatedImagePlayer>();
	for (auto [entity, player] : players)
	{
		if (!player.NeedResolve())
			continue;

		player.resolvedGuid = player.imageGuid;
		player.resource.reset();
		player.resolveFailed = false;
		player.finished = false;
		if (player.imageGuid.empty())
			continue;

		auto source = Resolve(player.imageGuid);
		player.resource = source.animated;
		player.resolveFailed = !player.resource;
	}
}

namespace
{
	void CollectNodeImages(const UI::UiNode& node, const UI::ParseResult& parsed, std::unordered_set<std::string>& out)
	{
		if (!node.texture.empty())
			out.insert(node.texture);

		if (auto it = parsed.resolved.find(&node); it != parsed.resolved.end())
		{
			if (it->second.effective.texture && !it->second.effective.texture->empty())
				out.insert(*it->second.effective.texture);
			for (const auto& [state, fields] : it->second.states)
			{
				if (fields.texture && !fields.texture->empty())
					out.insert(*fields.texture);
			}
		}

		for (const auto& child : node.children)
			CollectNodeImages(child, parsed, out);
	}

	bool HasStaleImage(const std::shared_ptr<const Render2D::UIImageTable>& table)
	{
		if (!table)
			return false;
		for (const auto& [reference, source] : *table)
		{
			if (source.animated && source.animated->IsStale())
				return true;
		}
		return false;
	}
}

void Render2DResourceSystem::ResolveUIImages()
{
	struct PendingTable
	{
		Entity entity;
		std::shared_ptr<UI::ParseResult> document;
		std::shared_ptr<const Render2D::UIImageTable> table;
	};
	std::vector<PendingTable> pending;

	auto views = m_world->getViewWith<UIViewRuntime>();
	for (auto [entity, runtime] : views)
	{
		auto* existing = entity.tryGetComponent<UIViewImageTable>();
		if (!runtime.document || !runtime.document->ok)
		{
			if (existing && existing->document)
			{
				existing->document.reset();
				existing->table.reset();
			}
			continue;
		}

		if (existing && existing->document == runtime.document && !HasStaleImage(existing->table))
			continue;

		std::unordered_set<std::string> references;
		CollectNodeImages(runtime.document->document.root, *runtime.document, references);

		auto table = std::make_shared<Render2D::UIImageTable>();
		for (const auto& reference : references)
		{
			auto source = Resolve(reference);
			if (source.IsValid())
				table->emplace(reference, std::move(source));
		}

		if (existing)
		{
			existing->document = runtime.document;
			existing->table = std::move(table);
		}
		else
		{
			// 遍历中不加组件，循环结束后统一加
			pending.push_back({ entity, runtime.document, std::move(table) });
		}
	}

	for (auto& item : pending)
	{
		auto& images = item.entity.addComponent<UIViewImageTable>();
		images.document = std::move(item.document);
		images.table = std::move(item.table);
	}
}

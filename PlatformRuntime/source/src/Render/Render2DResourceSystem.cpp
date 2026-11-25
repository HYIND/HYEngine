#include "Render/Systems/Render2DResourceSystem.h"

#include "ECSCore/World.h"
#include "Components/UIView.h"
#include "Render/Components/Sprite.h"
#include "Render/Components/AnimatedImagePlayer.h"

#include <iostream>
#include <set>
#include <unordered_set>
#include <utility>

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
	// 一处图像用法：图像引用 + 区域名（区域为空 = 整张图）
	using ImageUse = std::pair<std::string, std::string>;

	void AddUse(std::set<ImageUse>& out, const std::string& image, const std::string& region)
	{
		if (!image.empty())
			out.emplace(image, region);
	}

	// 和 RendererUI::DrawNode 选图的规则一致：图来自样式就用样式的 region，图来自节点就用节点的 region；
	// 状态里只写了 region 时，是在基础态的图上换区域
	void CollectNodeImages(const UI::UiNode& node, const UI::ParseResult& parsed, std::set<ImageUse>& out)
	{
		AddUse(out, node.image, node.region);

		if (auto it = parsed.resolved.find(&node); it != parsed.resolved.end())
		{
			const UI::StyleFields& base = it->second.effective;
			const bool baseHasImage = base.image && !base.image->empty();
			const std::string baseImage = baseHasImage ? *base.image : node.image;
			const std::string baseRegion = base.region ? *base.region : (baseHasImage ? std::string() : node.region);
			AddUse(out, baseImage, baseRegion);

			for (const auto& [state, fields] : it->second.states)
			{
				if (fields.image && !fields.image->empty())
					AddUse(out, *fields.image, fields.region ? *fields.region : std::string());
				else if (fields.region)
					AddUse(out, baseImage, *fields.region);
			}
		}

		for (const auto& child : node.children)
			CollectNodeImages(child, parsed, out);
	}

	void WarnRegion(const UI::ParseResult& parsed, const ImageUse& use, const char* message)
	{
		std::cerr << "[UI 图像] " << parsed.document.sourcePath << "：image \"" << use.first << "\" 的 region \""
			<< use.second << "\"" << message << std::endl;
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

		std::set<ImageUse> uses;
		CollectNodeImages(runtime.document->document.root, *runtime.document, uses);

		auto table = std::make_shared<Render2D::UIImageTable>();
		for (const auto& [reference, region] : uses)
		{
			if (table->contains(reference))
				continue;
			auto source = Resolve(reference);
			if (source.IsValid())
				table->emplace(reference, std::move(source));
		}

		// 区域名只在文档加载（换文档 / 动图资产过期）时检查一次，找不到的退回整张图
		for (const auto& use : uses)
		{
			if (use.second.empty())
				continue;
			auto found = table->find(use.first);
			if (found == table->end())
				continue;	// 图本身没解析出来，Resolve 已经报过了
			if (found->second.animated)
				WarnRegion(*runtime.document, use, " 无效：动图没有区域，忽略");
			else if (found->second.sprite && !found->second.sprite->HasRegion(use.second))
				WarnRegion(*runtime.document, use, " 不存在，按整张图画");
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

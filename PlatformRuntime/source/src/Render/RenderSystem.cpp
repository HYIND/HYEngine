#include "Render/Systems/RenderSystem.h"
#include "Components/UIView.h"
#include "Render/Systems/ParticleSystem.h"
#include "CommonComponent.h"
#include "PlatformRuntimeComponents.h"
#include "ECSCore/World.h"
#include "Systems/ScriptSystem.h"
#include "Render/Systems/Render2DResourceSystem.h"
#include "VulkanRenderEngine/General/RenderHelper.h"

#include <algorithm>
#include <cfloat>
#include <execution>
#include <tuple>
#include <functional>

using namespace Render;

RenderableBaseModule::RenderableBaseModule(World* world) :_world(world) {}

void RenderableBaseModule::Register(sol::state_view lua, sol::usertype<Entity>& entityType)
{
	sol::table render = lua["Render"].get_or_create<sol::table>();

	static auto DefaultCube = RenderHelper::GetCubeModel(glm::vec3(0.5f), 1.0f, "");
	static auto DefaultSphere = RenderHelper::GetSphereModel(0.5f, 72, 36);

	auto setVisiable = [this](const Entity& entity, const std::string& field, bool value) ->bool {
		return SetVisiable(entity, field, value);
		};
	auto setColor = [this](const Entity& entity, const glm::vec3& color) ->bool {
		return SetColor(entity, color);
		};
	auto attach_model = [this](const Entity& entity, const std::string& modelName) ->bool {
		if (!IsValid(_world, entity))
			return false;

		if (modelName != "DefaultCube" && modelName != "DefaultSphere")
			return false;

		auto& comp = entity.hasComponent<RenderStaticModel>() ? entity.getComponent<RenderStaticModel>() : entity.addComponent<RenderStaticModel>();

		if (modelName == "DefaultCube")
			comp.model = DefaultCube->Clone(false, true);
		if (modelName == "DefaultSphere")
			comp.model = DefaultSphere->Clone(false, true);
		return true;
		};


	render.set_function("set_visible", setVisiable);
	render.set_function("set_color", setColor);
	render.set_function("attach_model", attach_model);

	entityType.set("set_visible", setVisiable);
	entityType.set("set_color", setColor);
}

bool RenderableBaseModule::SetVisiable(const Entity& entity, const std::string& field, bool value)
{
	if (!IsValid(_world, entity))
		return false;

	Renderable* comp = nullptr;

	if (field == "model")
	{
		comp = entity.tryGetComponent<RenderStaticModel>();
	}
	else if (field == "light")
	{
		comp = entity.tryGetComponent<RenderLight>();
	}
	else if (field == "sprite")
	{
		comp = entity.tryGetComponent<Sprite>();
	}
	else if (field == "animatedimage")
	{
		comp = entity.tryGetComponent<AnimatedImagePlayer>();
	}
	else if (field == "laserbeam")
	{
		comp = entity.tryGetComponent<LaserBeamEmitter>();
	}
	else if (field == "particle")
	{
		// 粒子不是实体上的组件（由 ParticleSystem 按发射器存放）：控制这个发射器的粒子渲不渲染，模拟照常进行
		auto* emitter = entity.tryGetComponent<ParticleEmitter>();
		if (!emitter)
			return false;
		emitter->visible = value;
		return true;
	}
	else if (field == "skybox")
	{
		comp = entity.tryGetComponent<SkyBox>();
	}

	if (!comp)
		return false;
	comp->visible = value;
	return true;
}

bool RenderableBaseModule::SetColor(const Entity& entity, const glm::vec3& color)
{
	if (!IsValid(_world, entity))
		return false;

	auto comp = entity.tryGetComponent<RenderStaticModel>();
	if (!comp || !comp->model)
		return false;

	for (auto& info : comp->model->GetMeshInfos())
	{
		if (info.material)
			info.material->SetAlbedo(color);
	}
	return true;
}

RenderSystem::RenderSystem()
	:
	_threadPool(0)
{}

void RenderSystem::onAttach(World& world) {
	if (auto scriptSystem = world.getSystem<ScriptSystem>())
	{
		scriptSystem->RegisterScriptModule("RenderableBaseModule", std::make_unique<RenderableBaseModule>(&world));
	}
}

void RenderSystem::SetTriBufferScene(std::shared_ptr<TripleBuffer<std::shared_ptr<Render::RenderSceneFrameData>>> triBuffer)
{
	_triBufferScene = std::move(triBuffer);
}

void RenderSystem::SetTriBufferUI(std::shared_ptr<TripleBuffer<std::shared_ptr<Render::RenderUIFrameData>>> triBuffer)
{
	_triBufferUI = std::move(triBuffer);
}

void RenderSystem::postUpdate(float deltaTime)
{
	if (!_threadPool.running())
		_threadPool.start();

	// 逻辑性能统计：各段在线程池里并行执行，各自计时写原子量；整段按墙钟另记
	LogicFrameProfile* profile = m_world->GetCurrentLogicProfile();
	const int64_t collectStart = profile ? LogicProfileNowUs() : 0;
	auto submit = [this, profile](LogicRenderSection section, auto fn)
		{
			return _threadPool.submit([profile, section, fn = std::move(fn)]
				{
					LogicRenderSectionScope scope(profile, section);
					fn();
				});
		};

	std::vector<std::shared_ptr<ThreadPool::SubmitHandle<void>>> tasks;

	// 3D 和 2D 写进同一份帧数据，最后一次提交：渲染端取到的 3D / 2D 一定来自同一个逻辑帧
	auto triBufferScene = _triBufferScene;
	auto sceneFrame = triBufferScene ? triBufferScene->acquireWriteBuffer() : nullptr;
	auto framebuffer3D = sceneFrame ? sceneFrame->scene3D : nullptr;
	if (framebuffer3D)
	{
		// 主相机以 World 记录的为准（编辑器相机、SetMainCamera 设的都在这里）；
		// 场景文件里的相机反序列化时会自带 TagMainCamera，同时可能有多台带 tag，按 tag 取只作后备。
		Entity maincamera = m_world->GetMainCameraEntity();
		if (!m_world->IsValidWorldEntity(maincamera) || !maincamera.hasComponents<Transform, CameraComponent>())
		{
			auto entities = m_world->getEntitiesWith<TagMainCamera, Transform, CameraComponent>();
			maincamera = entities.empty() ? Entity() : entities[0];
		}
		if (maincamera)
		{

			tasks.push_back(submit(LogicRenderSection::Model, [&, maincamera] {ProcessModel(framebuffer3D, maincamera); }));
			tasks.push_back(submit(LogicRenderSection::Light, [&, maincamera] {ProcessLight(framebuffer3D, maincamera); }));
			tasks.push_back(submit(LogicRenderSection::Particle, [&, maincamera] {ProcessParticle(framebuffer3D, maincamera); }));
			tasks.push_back(submit(LogicRenderSection::LaserBeam, [&, maincamera] {ProcessLaserBeam(framebuffer3D, maincamera); }));
			tasks.push_back(submit(LogicRenderSection::Skybox, [&, maincamera] {ProcessSkybox(framebuffer3D, maincamera); }));
			tasks.push_back(submit(LogicRenderSection::DDGIVolume, [&, maincamera] {ProcessDDGIVolume(framebuffer3D, maincamera); }));
			tasks.push_back(submit(LogicRenderSection::SyncCamera, [&, maincamera] {SyncGLCamera(framebuffer3D, maincamera); }));
		}
	}

	auto framebuffer2D = sceneFrame ? sceneFrame->scene2D : nullptr;
	if (framebuffer2D)
	{
		tasks.push_back(submit(LogicRenderSection::Sprite, [&] {ProcessSprite(framebuffer2D); }));
		tasks.push_back(submit(LogicRenderSection::AnimatedImage, [&] {ProcessAnimatedImage(framebuffer2D); }));
		tasks.push_back(submit(LogicRenderSection::Camera2D, [&] {ProcessCamera2D(framebuffer2D); }));
		tasks.push_back(submit(LogicRenderSection::DebugLines, [&] {ProcessDebugLines(framebuffer2D); }));
	}

	auto triBufferUI = _triBufferUI;
	auto framebufferUI = triBufferUI ? triBufferUI->acquireWriteBuffer() : nullptr;
	if (framebufferUI)
	{
		tasks.push_back(submit(LogicRenderSection::UI, [&] {ProcessUI(framebufferUI); }));
	}

	if (!tasks.empty())
	{
		for (auto& task : tasks)
			task->get();
	}

	const int64_t submitStart = profile ? LogicProfileNowUs() : 0;

	if (triBufferScene && sceneFrame)
		triBufferScene->submitWriteBuffer();

	if (triBufferUI && framebufferUI)
		triBufferUI->submitWriteBuffer();

	if (profile)
	{
		const int64_t now = LogicProfileNowUs();
		profile->renderCollected = true;
		profile->renderCollectUs = submitStart - collectStart;
		profile->renderSubmitUs = now - submitStart;
		profile->submitted3D = triBufferScene && framebuffer3D;
		profile->submitted2D = triBufferScene && framebuffer2D;
		profile->submittedUI = triBufferUI && framebufferUI;
	}
}

void RenderSystem::ProcessUI(std::shared_ptr<Render::RenderUIFrameData> framebuffer)
{
	if (!framebuffer)
		return;

	auto entities = m_world->getViewWith<UIView, UIViewRuntime>();
	for (auto [entity, view, runtime] : entities)
	{
		if (!view.enabled || runtime.faulted || !runtime.document || !runtime.document->ok)
			continue;

		Render::RenderUIFrameData::View item;
		item.document = runtime.document;
		item.instance = runtime.instance;
		if (const auto* images = entity.tryGetComponent<UIViewImageTable>(); images && images->document == runtime.document)
			item.images = images->table;
		item.layer = view.layer;
		framebuffer->views.push_back(std::move(item));
	}

	std::stable_sort(framebuffer->views.begin(), framebuffer->views.end(),
		[](const Render::RenderUIFrameData::View& a, const Render::RenderUIFrameData::View& b)
		{
			return a.layer < b.layer;
		});
}

namespace
{
	// 2D 放置：位置 + 旋转后的两根单位轴（y 朝下，角度为度，正方向顺时针）
	struct Placement2D
	{
		glm::vec2 position = glm::vec2(0.0f);
		glm::vec2 ex = glm::vec2(1.0f, 0.0f);
		glm::vec2 ey = glm::vec2(0.0f, 1.0f);
	};

	Placement2D MakePlacement(const Transform2D& transform, const glm::vec2& offset, bool flipX, bool flipY)
	{
		Placement2D p;
		const float r = glm::radians(transform.rotation);
		const float c = std::cos(r), s = std::sin(r);
		p.ex = glm::vec2(c, s);
		p.ey = glm::vec2(-s, c);
		// offset 是渲染中心相对 Transform2D 的偏移，跟着物体一起旋转
		p.position = transform.position + p.ex * offset.x + p.ey * offset.y;
		// 翻转以轴心为对称中心
		if (flipX) p.ex = -p.ex;
		if (flipY) p.ey = -p.ey;
		return p;
	}

	// 在放置坐标系里，局部矩形 [l0, l1]（相对轴心）映射 uv 子矩形
	VKRenderContext::Sprite2DDrawItem MakeQuad(const Placement2D& p, const glm::vec2& l0, const glm::vec2& l1, const glm::vec4& uv)
	{
		VKRenderContext::Sprite2DDrawItem item;
		item.origin = p.position + p.ex * l0.x + p.ey * l0.y;
		item.axisX = p.ex * (l1.x - l0.x);
		item.axisY = p.ey * (l1.y - l0.y);
		item.uvRect = uv;
		return item;
	}

	// 把一帧精灵展开成四边形（Simple 一个，Sliced 最多九个）
	void EmitSpriteQuads(std::vector<VKRenderContext::Sprite2DDrawItem>& out,
		const Render2D::SpriteFrame& frame, const Placement2D& placement,
		const glm::vec2& worldSize, const glm::vec2& pivot, bool sliced, const glm::vec2& scale,
		const VKRenderContext::Sprite2DDrawItem& common)
	{
		const glm::vec2 l0 = -pivot * worldSize;
		const glm::vec2 l1 = l0 + worldSize;

		auto push = [&](const glm::vec2& a, const glm::vec2& b, const glm::vec4& uv)
			{
				auto item = MakeQuad(placement, a, b, uv);
				item.texture = common.texture;
				item.color = common.color;
				item.blend = common.blend;
				item.layer = common.layer;
				item.order = common.order;
				item.sequence = common.sequence;
				out.push_back(std::move(item));
			};

		if (!sliced || !frame.HasBorder())
		{
			push(l0, l1, frame.uvRect);
			return;
		}

		// 九宫格：边距按像素（1 像素 = 1 单位）再乘物体缩放，尺寸不够时按比例压缩边距
		const glm::vec2 absSize = glm::abs(worldSize);
		const glm::vec2 absScale = glm::abs(scale);
		glm::vec4 b(frame.border.x * absScale.x, frame.border.y * absScale.y, frame.border.z * absScale.x, frame.border.w * absScale.y);
		if (b.x + b.z > absSize.x && b.x + b.z > 0.0f) { const float k = absSize.x / (b.x + b.z); b.x *= k; b.z *= k; }
		if (b.y + b.w > absSize.y && b.y + b.w > 0.0f) { const float k = absSize.y / (b.y + b.w); b.y *= k; b.w *= k; }

		const glm::vec2 sign(worldSize.x < 0.0f ? -1.0f : 1.0f, worldSize.y < 0.0f ? -1.0f : 1.0f);
		const float xs[4] = { l0.x, l0.x + b.x * sign.x, l1.x - b.z * sign.x, l1.x };
		const float ys[4] = { l0.y, l0.y + b.y * sign.y, l1.y - b.w * sign.y, l1.y };

		const glm::vec2 uvPerPixel = glm::vec2(frame.uvRect.z - frame.uvRect.x, frame.uvRect.w - frame.uvRect.y) / glm::max(frame.size, glm::vec2(1.0f));
		const float us[4] = { frame.uvRect.x, frame.uvRect.x + frame.border.x * uvPerPixel.x, frame.uvRect.z - frame.border.z * uvPerPixel.x, frame.uvRect.z };
		const float vs[4] = { frame.uvRect.y, frame.uvRect.y + frame.border.y * uvPerPixel.y, frame.uvRect.w - frame.border.w * uvPerPixel.y, frame.uvRect.w };

		for (int row = 0; row < 3; ++row)
		{
			for (int col = 0; col < 3; ++col)
			{
				if (std::abs(xs[col + 1] - xs[col]) <= 1e-5f || std::abs(ys[row + 1] - ys[row]) <= 1e-5f)
					continue;
				push(glm::vec2(xs[col], ys[row]), glm::vec2(xs[col + 1], ys[row + 1]), glm::vec4(us[col], vs[row], us[col + 1], vs[row + 1]));
			}
		}
	}

	glm::vec2 ResolveWorldSize(const glm::vec2& requested, const glm::vec2& frameSize, const glm::vec2& scale)
	{
		const glm::vec2 base(requested.x > 0.0f ? requested.x : frameSize.x, requested.y > 0.0f ? requested.y : frameSize.y);
		return base * scale;
	}
}

Entity RenderSystem::Pick2D(World& world, const glm::vec2& point)
{
	Entity best;
	bool found = false;
	int bestLayer = 0;
	int bestOrder = 0;
	uint32_t bestSequence = 0;

	auto consider = [&](Entity entity, const Transform2D& transform, const RenderBase2D& base, const Render2D::SpriteFrame& frame,
		const glm::vec2& requestedSize, const glm::vec2& pivot, bool flipX, bool flipY)
		{
			if (!frame.IsValid())
				return;
			const glm::vec2 worldSize = ResolveWorldSize(requestedSize, frame.size, transform.scale);
			if (worldSize.x == 0.0f || worldSize.y == 0.0f)
				return;

			// 点换到放置坐标系里（ex / ey 是单位轴，翻转时取反），再和相对轴心的矩形比
			const Placement2D placement = MakePlacement(transform, base.offset, flipX, flipY);
			const glm::vec2 d = point - placement.position;
			const glm::vec2 local(glm::dot(d, placement.ex), glm::dot(d, placement.ey));
			const glm::vec2 l0 = -pivot * worldSize;
			const glm::vec2 l1 = l0 + worldSize;
			const glm::vec2 lo = glm::min(l0, l1);
			const glm::vec2 hi = glm::max(l0, l1);
			if (local.x < lo.x || local.x > hi.x || local.y < lo.y || local.y > hi.y)
				return;

			const uint32_t sequence = static_cast<uint32_t>(entity.getId());
			if (!found || std::tie(base.layer, base.internalZOrder, sequence) > std::tie(bestLayer, bestOrder, bestSequence))
			{
				found = true;
				best = entity;
				bestLayer = base.layer;
				bestOrder = base.internalZOrder;
				bestSequence = sequence;
			}
		};

	for (auto [entity, transform, sprite] : world.getViewWith<Transform2D, Sprite>())
	{
		if (!sprite.visible || !sprite.resource || sprite.color.a <= 0.0f)
			continue;
		const Render2D::SpriteFrame frame = sprite.ResolveFrame();
		consider(entity, transform, sprite, frame, sprite.size, sprite.overridePivot ? sprite.pivot : frame.pivot, sprite.flipX, sprite.flipY);
	}
	for (auto [entity, transform, player] : world.getViewWith<Transform2D, AnimatedImagePlayer>())
	{
		if (!player.visible || !player.resource || player.color.a <= 0.0f)
			continue;
		consider(entity, transform, player, player.ResolveFrame(), player.size, player.pivot, player.flipX, player.flipY);
	}
	return best;
}

std::vector<Entity> RenderSystem::Pick2DRect(World& world, const std::function<glm::vec2(const glm::vec2&)>& toRectSpace,
	const glm::vec2& rectMin, const glm::vec2& rectMax)
{
	const glm::vec2 lo = glm::min(rectMin, rectMax);
	const glm::vec2 hi = glm::max(rectMin, rectMax);
	std::vector<Entity> result;

	// 四边形（Get2DBounds 的四个角映射之后，平行四边形）和轴对齐矩形的分离轴判定：矩形的 x / y 轴 + 四边形的两条边的法线
	auto overlaps = [&](const Bounds2D& b)
		{
			auto separated = [&](const glm::vec2& axis)
				{
					const glm::vec2 rc[4] = { lo, { hi.x, lo.y }, hi, { lo.x, hi.y } };
					float aMin = FLT_MAX, aMax = -FLT_MAX, bMin = FLT_MAX, bMax = -FLT_MAX;
					for (int i = 0; i < 4; ++i)
					{
						const float pa = glm::dot(rc[i], axis);
						const float pb = glm::dot(b.corners[i], axis);
						aMin = std::min(aMin, pa); aMax = std::max(aMax, pa);
						bMin = std::min(bMin, pb); bMax = std::max(bMax, pb);
					}
					return aMax < bMin || bMax < aMin;
				};
			const glm::vec2 e0 = b.corners[1] - b.corners[0];
			const glm::vec2 e1 = b.corners[3] - b.corners[0];
			return !separated({ 1.0f, 0.0f }) && !separated({ 0.0f, 1.0f })
				&& !separated({ -e0.y, e0.x }) && !separated({ -e1.y, e1.x });
		};

	auto consider = [&](Entity entity)
		{
			Bounds2D bounds;
			if (!Get2DBounds(world, entity, bounds))
				return;
			for (glm::vec2& corner : bounds.corners)
				corner = toRectSpace(corner);
			if (overlaps(bounds))
				result.push_back(entity);
		};
	for (auto [entity, transform, sprite] : world.getViewWith<Transform2D, Sprite>())
	{
		if (!sprite.visible || !sprite.resource || sprite.color.a <= 0.0f)
			continue;
		consider(entity);
	}
	for (auto [entity, transform, player] : world.getViewWith<Transform2D, AnimatedImagePlayer>())
	{
		if (!player.visible || !player.resource || player.color.a <= 0.0f)
			continue;
		consider(entity);
	}

	std::sort(result.begin(), result.end(), [](const Entity& a, const Entity& b) { return a.getId() < b.getId(); });
	result.erase(std::unique(result.begin(), result.end()), result.end());
	return result;
}

bool RenderSystem::Get2DBounds(World& world, Entity entity, Bounds2D& out)
{
	if (!entity || !entity.isValid() || !world.IsValidWorldEntity(entity))
		return false;
	const auto* transform = entity.tryGetComponent<Transform2D>();
	if (!transform)
		return false;

	// 与 Pick2D 的 consider 相同：放置坐标系里相对轴心的矩形 [l0, l1]
	auto build = [&](const RenderBase2D& base, const Render2D::SpriteFrame& frame,
		const glm::vec2& requestedSize, const glm::vec2& pivot, bool flipX, bool flipY) -> bool
		{
			if (!frame.IsValid())
				return false;
			const glm::vec2 worldSize = ResolveWorldSize(requestedSize, frame.size, transform->scale);
			if (worldSize.x == 0.0f || worldSize.y == 0.0f)
				return false;

			const Placement2D placement = MakePlacement(*transform, base.offset, flipX, flipY);
			const glm::vec2 l0 = -pivot * worldSize;
			const glm::vec2 l1 = l0 + worldSize;
			auto at = [&](float x, float y) { return placement.position + placement.ex * x + placement.ey * y; };
			out.corners[0] = at(l0.x, l0.y);
			out.corners[1] = at(l1.x, l0.y);
			out.corners[2] = at(l1.x, l1.y);
			out.corners[3] = at(l0.x, l1.y);
			out.pivot = placement.position;
			return true;
		};

	if (const auto* sprite = entity.tryGetComponent<Sprite>(); sprite && sprite->resource)
	{
		const Render2D::SpriteFrame frame = sprite->ResolveFrame();
		if (build(*sprite, frame, sprite->size, sprite->overridePivot ? sprite->pivot : frame.pivot, sprite->flipX, sprite->flipY))
			return true;
	}
	if (const auto* player = entity.tryGetComponent<AnimatedImagePlayer>(); player && player->resource)
		return build(*player, player->ResolveFrame(), player->size, player->pivot, player->flipX, player->flipY);
	return false;
}

void RenderSystem::pushDebugLine(const Line2& line)
{
	LockGuard guard(_debugLineLock);
	_DebugLines.push_back(line);
}

void RenderSystem::ProcessSprite(std::shared_ptr<Render::Render2DFrameData> framebuffer)
{
	auto entities = m_world->getViewWith<Transform2D, Sprite>();

	std::vector<VKRenderContext::Sprite2DDrawItem> temp;
	temp.reserve(entities.size());
	for (auto [entity, transform, sprite] : entities)
	{
		if (!sprite.visible || !sprite.resource || sprite.color.a <= 0.0f)
			continue;

		const Render2D::SpriteFrame frame = sprite.ResolveFrame();
		if (!frame.IsValid())
			continue;

		const glm::vec2 worldSize = ResolveWorldSize(sprite.size, frame.size, transform.scale);
		if (worldSize.x == 0.0f || worldSize.y == 0.0f)
			continue;

		VKRenderContext::Sprite2DDrawItem common;
		common.texture = frame.texture;
		common.color = sprite.color;
		common.blend = sprite.blend;
		common.layer = sprite.layer;
		common.order = sprite.internalZOrder;
		common.sequence = static_cast<uint32_t>(entity.getId());

		const glm::vec2 pivot = sprite.overridePivot ? sprite.pivot : frame.pivot;
		const Placement2D placement = MakePlacement(transform, sprite.offset, sprite.flipX, sprite.flipY);
		EmitSpriteQuads(temp, frame, placement, worldSize, pivot, sprite.drawMode == SpriteDrawMode::Sliced, transform.scale, common);
	}

	if (temp.empty())
		return;
	LockGuard guard(_D2D_SpinLock);
	framebuffer->sprites.append_range(std::move(temp));
	framebuffer->hasContent = true;
}

void RenderSystem::ProcessAnimatedImage(std::shared_ptr<Render::Render2DFrameData> framebuffer)
{
	auto entities = m_world->getViewWith<Transform2D, AnimatedImagePlayer>();

	std::vector<VKRenderContext::Sprite2DDrawItem> temp;
	temp.reserve(entities.size());
	for (auto [entity, transform, player] : entities)
	{
		if (!player.visible || !player.resource || player.color.a <= 0.0f)
			continue;

		const Render2D::SpriteFrame frame = player.ResolveFrame();
		if (!frame.IsValid())
			continue;

		const glm::vec2 worldSize = ResolveWorldSize(player.size, frame.size, transform.scale);
		if (worldSize.x == 0.0f || worldSize.y == 0.0f)
			continue;

		VKRenderContext::Sprite2DDrawItem common;
		common.texture = frame.texture;
		common.color = player.color;
		common.blend = player.blend;
		common.layer = player.layer;
		common.order = player.internalZOrder;
		common.sequence = static_cast<uint32_t>(entity.getId());

		const Placement2D placement = MakePlacement(transform, player.offset, player.flipX, player.flipY);
		EmitSpriteQuads(temp, frame, placement, worldSize, player.pivot, false, transform.scale, common);
	}

	if (temp.empty())
		return;
	LockGuard guard(_D2D_SpinLock);
	framebuffer->sprites.append_range(std::move(temp));
	framebuffer->hasContent = true;
}

VKRenderContext::Camera2DData RenderSystem::GetActiveCamera2D(World& world, bool* found)
{
	// primary 且 priority 最大的那台；一台都没有时保持默认（等同旧的 MapBoundary：1000x600 拉伸）
	Entity chosen;
	const Camera2D* camera = nullptr;
	for (auto [entity, candidate] : world.getViewWith<Camera2D>())
	{
		if (!candidate.primary)
			continue;
		if (!camera || candidate.priority > camera->priority)
		{
			camera = &candidate;
			chosen = entity;
		}
	}

	if (found)
		*found = camera != nullptr;

	VKRenderContext::Camera2DData data;
	if (!camera)
		return data;

	data.designSize = glm::max(camera->designSize, glm::vec2(1.0f));
	data.zoom = std::max(camera->zoom, 1e-4f);
	data.aspectMode = camera->aspectMode;
	data.pixelSnap = camera->pixelSnap;
	data.center = data.designSize * 0.5f;
	data.rotation = 0.0f;
	if (const auto* transform = chosen.tryGetComponent<Transform2D>())
	{
		data.center = transform->position;
		data.rotation = transform->rotation;
	}
	return data;
}

void RenderSystem::ProcessCamera2D(std::shared_ptr<Render::Render2DFrameData> framebuffer)
{
	bool found = false;
	auto data = GetActiveCamera2D(*m_world, &found);
	if (!found)
		return;

	// 只有这个任务写 camera，不用和精灵共用锁
	framebuffer->camera = data;
}

void RenderSystem::ProcessDebugLines(std::shared_ptr<Render::Render2DFrameData> framebuffer)
{
	std::vector<Line2> lines;
	{
		LockGuard guard(_debugLineLock);
		lines.swap(_DebugLines);
	}
	if (lines.empty())
		return;

	std::vector<VKRenderContext::Line2DDrawItem> temp;
	temp.reserve(lines.size());
	for (const auto& line : lines)
	{
		VKRenderContext::Line2DDrawItem item;
		item.from = line.pos1;
		item.to = line.pos2;
		item.color = line.color;
		item.width = line.width;
		temp.push_back(item);
	}

	LockGuard guard(_D2D_SpinLock);
	framebuffer->lines.append_range(std::move(temp));
	framebuffer->hasContent = true;
}

void RenderSystem::ProcessStaticModel(std::shared_ptr<Render3DFrameData> framebuffer, Entity maincamera)
{
	auto view = m_world->getViewWith<Transform, RenderStaticModel>();
	std::vector<std::shared_ptr<VKRenderContext::RenderContext>> temp;
	temp.reserve(view.size());
	for (auto [entity, transform, renderModel] : view)
	{
		if (!renderModel.visible || !renderModel.model) continue;

		auto renderdata = std::make_shared<VKRenderContext::StaticModelRenderData>();

		renderdata->transform = transform.GetMatrix() * renderModel.trans;
		renderdata->transformHistory = renderModel.transformHistory;
		renderdata->entityId = entity.getId();

		renderdata->model = renderModel.model;

		auto context = std::make_shared<VKRenderContext::RenderContext>();
		context->type = VKRenderContext::RenderContextType::StaticModel;
		context->data = renderdata;

		temp.emplace_back(std::move(context));
	}
	LockGuard guard(_Scene3D_SpinLock);
	framebuffer->Scene3D_Contexts.append_range(temp);
}

void RenderSystem::ProcessModel(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera)
{
	ProcessStaticModel(framebuffer, maincamera);
	ProcessSkinnedModel(framebuffer, maincamera);
}

void RenderSystem::ProcessSkinnedModel(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera)
{
	auto view = m_world->getViewWith<Transform, RenderSkinnedModel>();
	if (view.empty())
		return;

	std::vector<std::shared_ptr<VKRenderContext::RenderContext>> temp;
	temp.reserve(view.size());

	// 每个实体投递一个「合成 per-mesh palette」的任务：等整个遍历结束后再统一并行执行。
	// 任务只写自己那个 renderdata->palettes，彼此没有交集。
	std::vector<std::function<void()>> paletteTasks;
	paletteTasks.reserve(view.size());

	for (auto [entity, transform, renderModel] : view)
	{
		if (!renderModel.visible || !renderModel.model)
			continue;

		auto renderdata = std::make_shared<VKRenderContext::SkinnedModelRenderData>();

		renderdata->transform = transform.GetMatrix() * renderModel.trans;
		renderdata->transformHistory = renderModel.transformHistory;
		renderdata->entityId = entity.getId();
		renderdata->model = renderModel.model;

		if (entity.hasComponent<SkeletonPose>())
		{
			auto& pose = entity.getComponent<SkeletonPose>();
			if (!pose.globalTransforms.empty())
			{
				// palette 是 per-mesh
				const std::vector<glm::mat4>* globalTransforms = &pose.globalTransforms;
				auto& meshInfos = renderdata->model->GetMeshInfos();
				renderdata->palettes.resize(meshInfos.size());

				for (std::size_t meshIndex = 0; meshIndex < meshInfos.size(); ++meshIndex)
				{
					const std::shared_ptr<SkinnedMesh>& mesh = meshInfos[meshIndex].mesh;
					if (!mesh)
						continue;

					auto palette = std::make_shared<VKRenderContext::BonePalette>();
					const std::vector<glm::mat4>& inverseBindMatrices = mesh->GetInverseBindMatrices();

					std::size_t count = globalTransforms->size();
					if (!inverseBindMatrices.empty() && inverseBindMatrices.size() < count)
						count = inverseBindMatrices.size();
					palette->matrices.resize(count);
					renderdata->palettes[meshIndex] = palette;

					paletteTasks.emplace_back([globalTransforms, mesh, palette, count]()
						{
							const std::vector<glm::mat4>& bones = mesh->GetInverseBindMatrices();
							for (std::size_t boneIndex = 0; boneIndex < count; ++boneIndex)
							{
								// 网格没有导出骨骼时（inverseBindMatrices 为空）没有顶点会引用骨骼，
								// 这里给单位阵让顶点保持网格局部空间。
								const glm::mat4 boneMatrix = bones.empty()
									? glm::mat4(1.0f)
									: (*globalTransforms)[boneIndex] * bones[boneIndex];

								VKRenderContext::BoneMatrices& bone = palette->matrices[boneIndex];
								bone.matrices = boneMatrix;
								bone.normalMatrices = glm::mat4(glm::transpose(glm::inverse(glm::mat3(boneMatrix))));
							}
						});
				}
			}

			renderdata->poseHistory = pose.history;
		}

		auto context = std::make_shared<VKRenderContext::RenderContext>();
		context->type = VKRenderContext::RenderContextType::SkinnedModel;
		context->data = renderdata;

		temp.emplace_back(std::move(context));
	}

	if (!paletteTasks.empty())
	{
		std::for_each(std::execution::par_unseq, paletteTasks.begin(), paletteTasks.end(),
			[](const std::function<void()>& task) { task(); });
	}

	LockGuard guard(_Scene3D_SpinLock);
	framebuffer->Scene3D_Contexts.append_range(temp);
}

void RenderSystem::ProcessLight(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera)
{
	auto views = m_world->getViewWith<Transform, RenderLight>();
	auto& cameraComponent = maincamera.getComponent<CameraComponent>();

	std::vector<std::shared_ptr<VKRenderContext::RenderContext>> temp;
	temp.reserve(views.size());
	for (auto [entity, transform, renderLight] : views)
	{
		if (!renderLight.visible)
			continue;

		static auto setLightBaseParams = [](Light* light, BaseLightData* data) {
			light->SetColor(data->color);
			light->SetShadowMapSize(data->shadowMapSize);
			light->SetCastShadow(data->castShadow);
			light->SetShadowNormalBias(data->shadowNormalBias);
			light->SetShadowSlopeBias(data->shadowSlopeBias);
			light->SetVolumeLightEnable(data->volumeLightEnable);
			light->SetVolumeLightEffectByAtmospher(data->volumeLightEffectByAtmospher);
			light->SetVolumetricScatteringIntensity(data->volumetricScatteringIntensity);
			};

		switch (renderLight.type)
		{
		case LightType::Directional:
		{
			if (auto data = renderLight.GetData<DirectionalLightData>())
			{
				auto postion = transform.position;
				auto direction = transform.GetDirection();

				auto renderdata = std::make_shared<VKRenderContext::DirLightRenderData>();
				renderdata->light = std::make_shared<DirLight>(direction, data->color, data->luxIntensity);
				setLightBaseParams(renderdata->light.get(), data);
				renderdata->light->SetCascadeLevel(data->cascadeLevel);
				renderdata->light->SetShadowDistance(data->shadowDistance);
				renderdata->light->SetCascadeSplitLambda(data->cascadeSplitLambda);
				renderdata->renderCube = renderLight.renderCube;
				renderdata->entityId = entity.getId();

				auto context = std::make_shared<VKRenderContext::RenderContext>();
				context->type = VKRenderContext::RenderContextType::DirLight;
				context->data = renderdata;

				temp.emplace_back(std::move(context));
			}
			break;
		}
		case LightType::Point:
		{
			if (auto data = renderLight.GetData<PointLightData>())
			{
				auto position = transform.position;
				auto direction = transform.GetDirection();

				auto renderdata = std::make_shared<VKRenderContext::PointLightRenderData>();
				renderdata->light = std::make_shared<PointLight>(position, data->color, data->cdIntensity);
				setLightBaseParams(renderdata->light.get(), data);
				renderdata->renderCube = renderLight.renderCube;

				auto context = std::make_shared<VKRenderContext::RenderContext>();
				context->type = VKRenderContext::RenderContextType::PointLight;
				context->data = renderdata;

				temp.emplace_back(std::move(context));
			}
			break;
		}
		case LightType::Spot:
		{
			if (auto data = renderLight.GetData<SpotLightData>())
			{
				auto position = transform.position;
				auto direction = transform.GetDirection();

				auto renderdata = std::make_shared<VKRenderContext::SpotLightRenderData>();
				renderdata->light = std::make_shared<SpotLight>(position, direction, data->cutOffAngle, data->outercutOffAngle, data->color, data->cdIntensity);
				setLightBaseParams(renderdata->light.get(), data);
				renderdata->renderCube = renderLight.renderCube;

				auto context = std::make_shared<VKRenderContext::RenderContext>();
				context->type = VKRenderContext::RenderContextType::SpotLight;
				context->data = renderdata;

				temp.emplace_back(std::move(context));
			}
			break;
		}
		default:
			break;
		}

	}
	LockGuard guard(_Scene3D_SpinLock);
	framebuffer->Scene3D_Contexts.append_range(temp);
}

void RenderSystem::ProcessParticle(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera)
{
	auto particleSystem = m_world->getSystem<ParticleSystem>();
	if (!particleSystem)
		return;

	// 整帧粒子只读数据由 ParticleSystem::update 生成，这里只包一层交给渲染
	auto frame = particleSystem->GetFrameData();
	if (!frame || frame->batches.empty())
		return;

	auto renderdata = std::make_shared<VKRenderContext::ParticleBatchRenderData>();
	renderdata->frame = std::move(frame);

	auto context = std::make_shared<VKRenderContext::RenderContext>();
	context->type = VKRenderContext::RenderContextType::ParticleBatch;
	context->data = std::move(renderdata);

	LockGuard guard(_Scene3D_SpinLock);
	framebuffer->Scene3D_Contexts.push_back(std::move(context));
}

void RenderSystem::ProcessLaserBeam(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera)
{
	auto views = m_world->getViewWith<LaserBeamEmitter, Transform>();
	std::vector<std::shared_ptr<VKRenderContext::RenderContext>> temp;
	temp.reserve(views.size());
	for (auto [entity, emitter, transform] : views)
	{
		if (!emitter.visible || !emitter.visible || !emitter.properties)
			continue;

		auto renderdata = std::make_shared<VKRenderContext::EffectRenderData>();
		renderdata->properties = emitter.properties;

		auto context = std::make_shared<VKRenderContext::RenderContext>();
		context->type = VKRenderContext::RenderContextType::Effect;
		context->data = renderdata;

		temp.emplace_back(std::move(context));
	}
	LockGuard guard(_Scene3D_SpinLock);
	framebuffer->Scene3D_Contexts.append_range(temp);
}

void RenderSystem::ProcessSkybox(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera)
{
	auto view = m_world->getViewWith<SkyBox>();
	if (view.empty())
		return;

	for (auto [entity, skybox] : view)
	{
		if (!skybox.visible || !skybox.cube)
			continue;

		LockGuard guard(_Scene3D_SpinLock);
		framebuffer->skybox = skybox.cube;
		return;
	}
}

void RenderSystem::ProcessDDGIVolume(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera)
{
	auto view = m_world->getViewWith<Transform, DDGIVolumeComponent>();

	std::vector<DDGIVolumeDesc> temp;
	for (auto [entity, transform, volume] : view)
	{
		if (!volume.visible)
			continue;

		DDGIVolumeDesc desc;
		desc.id = DDGIVolumeIdFromEntity(entity.getId());
		desc.center = transform.position;
		desc.spacing = volume.spacing;
		// Gizmo 缩放拖动期间 scale != 1：按缩放实时预览，松开时编辑器把它折算进 counts
		desc.counts = glm::uvec3(glm::clamp(glm::round(glm::vec3(volume.counts) * glm::abs(transform.scale)),
			glm::vec3(1.0f), glm::vec3(255.0f)));
		desc.priority = volume.priority;
		desc.fadeCells = volume.fadeCells;
		desc.autoRelocate = volume.autoRelocate;
		desc.edits = volume.edits;
		temp.push_back(std::move(desc));
	}

	LockGuard guard(_Scene3D_SpinLock);
	framebuffer->ddgiVolumes = std::move(temp);
}

void RenderSystem::SyncGLCamera(std::shared_ptr<Render3DFrameData> framebuffer, Entity maincamera)
{
	auto& trans = maincamera.getComponent<Transform>();
	auto& camCom = maincamera.getComponent<CameraComponent>();

	CameraFrontData data(trans.GetDirection(), camCom.worldUp);

	framebuffer->position = trans.position;
	framebuffer->direction = data.cameraFront;
	framebuffer->directionUp = data.cameraUp;
	framebuffer->directionRight = data.cameraRight;
	framebuffer->nearPlane = camCom.nearPlane;
	framebuffer->farPlane = camCom.farPlane;
	framebuffer->fov = camCom.fov;

	//static int64_t lastPrintTime = 0.0f;
	//int64_t currentTime = Tool::GetTimestampMilliseconds();
	//if (currentTime - lastPrintTime >= 100)
	//{
	//	auto pos = camCom.camera.GetPosition();
	//	std::string posstr = std::format("MainCamera Position: {}, {}, {}\n", pos.x, pos.y, pos.z);
	//	std::cout << posstr;
	//	lastPrintTime = currentTime;
	//}

}


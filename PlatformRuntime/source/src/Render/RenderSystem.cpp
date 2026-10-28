#include "Render/Systems/RenderSystem.h"
#include "Components/UIView.h"
#include "Render/Systems/ParticleSystem.h"
#include "CommonComponent.h"
#include "PlatformRuntimeComponents.h"
#include "ECSCore/World.h"
#include "Systems/ScriptSystem.h"
#include "VulkanRenderEngine/General/RenderHelper.h"

using namespace Render;

RenderableBaseModule::RenderableBaseModule(World* world) :_world(world) {}

void RenderableBaseModule::Register(sol::state_view lua, sol::usertype<Entity>& entityType)
{
	sol::table render = lua["Render"].get_or_create<sol::table>();

	static auto DefaultCube = RenderHelper::GetCubeModel(glm::vec3(0.5f), 1.0f, "");

	auto setVisiable = [this](const Entity& entity, const std::string& field, bool value) ->bool {
		return SetVisiable(entity, field, value);
		};
	auto setColor = [this](const Entity& entity, const glm::vec3& color) ->bool {
		return SetColor(entity, color);
		};
	auto attach_model = [this](const Entity& entity, const std::string& modelName) ->bool {
		if (!IsValid(_world, entity))
			return false;

		if (modelName != "DefaultCube")
			return false;

		auto& comp = entity.hasComponent<RenderModel>() ? entity.getComponent<RenderModel>() : entity.addComponent<RenderModel>();

		if (modelName == "DefaultCube")
			comp.model = DefaultCube->Clone(false, true, true);

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
		comp = entity.tryGetComponent<RenderModel>();
	}
	else if (field == "light")
	{
		comp = entity.tryGetComponent<RenderLight>();
	}
	else if (field == "sprite")
	{
		comp = entity.tryGetComponent<Sprite>();
	}
	else if (field == "laserbeam")
	{
		comp = entity.tryGetComponent<LaserBeamEmitter>();
	}
	else if (field == "particle")
	{
		comp = entity.tryGetComponent<Particle>();
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

	auto comp = entity.tryGetComponent<RenderModel>();
	if (!comp || !comp->model)
		return false;

	for (auto& info : comp->model->getMeshInfos())
	{
		if (info.material)
			info.material->SetAlbedo(color);
	}
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

void RenderSystem::SetTriBuffer3D(std::shared_ptr<TripleBuffer<std::shared_ptr<Render::Render3DFrameData>>> triBuffer)
{
	_triBuffer3D = triBuffer;
}

void RenderSystem::SetTriBuffer2D(std::shared_ptr<TripleBuffer<std::shared_ptr<Render::Render2DFrameData>>> triBuffer)
{
	_triBuffer2D = triBuffer;
}

void RenderSystem::SetTriBufferUI(std::shared_ptr<TripleBuffer<std::shared_ptr<Render::RenderUIFrameData>>> triBuffer)
{
	_triBufferUI = std::move(triBuffer);
}

void RenderSystem::postUpdate(float deltaTime)
{
	if (!_threadPool.running())
		_threadPool.start();

	std::vector<std::shared_ptr<ThreadPool::SubmitHandle<void>>> tasks;

	auto triBuffer3D = _triBuffer3D;
	auto framebuffer3D = triBuffer3D ? triBuffer3D->acquireWriteBuffer() : nullptr;
	if (framebuffer3D)
	{
		auto entities = m_world->getEntitiesWith<TagMainCamera, Transform, CameraComponent>();
		if (!entities.empty())
		{
			Entity maincamera = entities[0];

			tasks.push_back(_threadPool.submit([&] {processModel(framebuffer3D, maincamera); }));
			tasks.push_back(_threadPool.submit([&] {processLight(framebuffer3D, maincamera); }));
			tasks.push_back(_threadPool.submit([&] {processParticle(framebuffer3D, maincamera); }));
			tasks.push_back(_threadPool.submit([&] {processLaserBeam(framebuffer3D, maincamera); }));
			//tasks.push_back(_threadPool.submit([&] {processFirstPersonVisual(framebuffer3D, maincamera); }));
			tasks.push_back(_threadPool.submit([&] {processSkybox(framebuffer3D, maincamera); }));
			tasks.push_back(_threadPool.submit([&] {SyncGLCamera(framebuffer3D, maincamera); }));
		}
	}

	auto triBuffer2D = _triBuffer2D;
	auto framebuffer2D = triBuffer2D ? triBuffer2D->acquireWriteBuffer() : nullptr;
	if (framebuffer2D)
	{
		tasks.push_back(std::move(_threadPool.submit([&] {processSprite(framebuffer2D); })));
		tasks.push_back(std::move(_threadPool.submit([&] {processGIFAnimation(framebuffer2D); })));
		tasks.push_back(std::move(_threadPool.submit([&] {processDebugLines(framebuffer2D); })));
	}

	auto triBufferUI = _triBufferUI;
	auto framebufferUI = triBufferUI ? triBufferUI->acquireWriteBuffer() : nullptr;
	if (framebufferUI)
	{
		tasks.push_back(std::move(_threadPool.submit([&] {processUI(framebufferUI); })));
	}

	if (!tasks.empty())
	{
		for (auto& task : tasks)
			task->get();
	}

	if (triBuffer3D && framebuffer3D)
		triBuffer3D->submitWriteBuffer();

	if (triBuffer2D && framebuffer2D)
		triBuffer2D->submitWriteBuffer();

	if (triBufferUI && framebufferUI)
		triBufferUI->submitWriteBuffer();
}

void RenderSystem::processUI(std::shared_ptr<Render::RenderUIFrameData> framebuffer)
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
		item.layer = view.layer;
		framebuffer->views.push_back(std::move(item));
	}

	std::stable_sort(framebuffer->views.begin(), framebuffer->views.end(),
		[](const Render::RenderUIFrameData::View& a, const Render::RenderUIFrameData::View& b)
		{
			return a.layer < b.layer;
		});
}

void RenderSystem::pushDebugLine(const Line2& line)
{
	_DebugLines.push_back(line);
}

void RenderSystem::processSprite(std::shared_ptr<Render::Render2DFrameData> framebuffer)
{
	auto entities = m_world->getViewWith<Transform2D, Sprite>();

	std::vector<std::shared_ptr<D2DRenderContext::RenderContext>> temp;
	temp.reserve(entities.size());
	for (auto [entity, transform, sprite] : entities)
	{
		if (!sprite.visible ||
			!sprite.bitmap ||
			sprite.opacity <= 0 ||
			sprite.width <= 0 ||
			sprite.height <= 0)
			continue;

		auto renderdata = std::make_shared<D2DRenderContext::SpriteRenderData>();
		renderdata->x = transform.position.x - sprite.offset.x;
		renderdata->y = transform.position.y - sprite.offset.y;
		renderdata->rotation = transform.rotation;
		renderdata->width = sprite.width;
		renderdata->height = sprite.height;
		renderdata->bitmap = sprite.bitmap;
		renderdata->opacity = sprite.opacity;

		auto context = std::make_shared<D2DRenderContext::RenderContext>();
		context->type = D2DRenderContext::RenderContextType::Sprite;
		context->layer = sprite.layer;
		context->internalZOrder = sprite.internalZOrder;
		context->data = renderdata;

		temp.emplace_back(std::move(context));
	}

	LockGuard guard(_D2D_SpinLock);
	framebuffer->D2D_Contexts.append_range(temp);
}

void RenderSystem::processGIFAnimation(std::shared_ptr<Render::Render2DFrameData> framebuffer)
{
	auto entities = m_world->getViewWith<Transform2D, GIFAnimator>();
	if (entities.empty())
		return;

	std::vector<std::shared_ptr<D2DRenderContext::RenderContext>> temp;
	temp.reserve(entities.size());
	for (auto [entity, transform, gifanimator] : entities)
	{
		if (!gifanimator.visible || !gifanimator.gifInfo)continue;

		UINT framecount = gifanimator.gifInfo->getFrameCount();
		if (framecount <= 0 || gifanimator.giftotalTime <= 0.f) continue;
		if (gifanimator.width <= 0 || gifanimator.height <= 0) continue;
		int64_t currenttime = Tool::GetTimestampMilliseconds();
		if (gifanimator.loopCount > 0 && currenttime > gifanimator.startTime + gifanimator.loopCount * gifanimator.giftotalTime) continue;
		if (gifanimator.opacity <= 0.f) continue;

		auto renderdata = std::make_shared<D2DRenderContext::GIFAnimationRenderData>();
		renderdata->x = transform.position.x - gifanimator.offset.x;
		renderdata->y = transform.position.y - gifanimator.offset.y;
		renderdata->rotation = transform.rotation;
		renderdata->width = gifanimator.width;
		renderdata->height = gifanimator.height;
		renderdata->opacity = gifanimator.opacity;
		renderdata->gifInfo = gifanimator.gifInfo;
		renderdata->startTime = gifanimator.startTime;
		renderdata->giftotalTime = gifanimator.giftotalTime;
		renderdata->loopCount = gifanimator.loopCount;

		auto context = std::make_shared<D2DRenderContext::RenderContext>();
		context->type = D2DRenderContext::RenderContextType::GIFAnimation;
		context->layer = gifanimator.layer;
		context->internalZOrder = gifanimator.internalZOrder;
		context->data = renderdata;

		temp.emplace_back(std::move(context));
	}
	LockGuard guard(_D2D_SpinLock);
	framebuffer->D2D_Contexts.append_range(temp);
}

void RenderSystem::processDebugLines(std::shared_ptr<Render::Render2DFrameData> framebuffer)
{
	std::vector<std::shared_ptr<D2DRenderContext::RenderContext>> temp;
	temp.reserve(_DebugLines.size());
	for (auto& line : _DebugLines)
	{
		auto renderdata = std::make_shared <D2DRenderContext::DebugLineRenderData>();
		renderdata->line_pos1 = line.pos1;
		renderdata->line_pos2 = line.pos2;

		auto context = std::make_shared<D2DRenderContext::RenderContext>();
		context->type = D2DRenderContext::RenderContextType::DebugLine;
		context->data = renderdata;

		temp.emplace_back(std::move(context));
	}
	_DebugLines.clear();
	LockGuard guard(_D2D_SpinLock);
	framebuffer->D2D_Contexts.append_range(temp);
}

void RenderSystem::processModel(std::shared_ptr<Render3DFrameData> framebuffer, Entity maincamera)
{
	auto entities = m_world->getEntitiesWith<Transform, RenderModel>();
	auto& cameraComponent = maincamera.getComponent<CameraComponent>();

	auto view = m_world->getViewWith<Transform, RenderModel>();
	std::vector<std::shared_ptr<VKRenderContext::RenderContext>> temp;
	temp.reserve(entities.size());
	for (auto [entity, transform, renderModel] : view)
	{
		if (!renderModel.visible || !renderModel.model) continue;

		auto renderdata = std::make_shared<VKRenderContext::SceneModelRenderData>();

		renderdata->transform = transform.GetMatrix() * renderModel.trans;
		renderdata->transformHistory = renderModel.transformHistory;

		renderdata->model = renderModel.model;

		if (entity.hasComponent<SkeletonAnimatorGroup>())
		{
			auto& animatorGroup = entity.getComponent<SkeletonAnimatorGroup>();
			for (auto& [name, anidata] : animatorGroup.animatorDatas)
			{
				if (!anidata || !anidata->animator || !anidata->enable)
					continue;
				renderdata->animatorViews.push_back(anidata->renderView);
			}
		}

		auto context = std::make_shared<VKRenderContext::RenderContext>();
		context->type = VKRenderContext::RenderContextType::Model;
		context->data = renderdata;

		temp.emplace_back(std::move(context));
	}
	LockGuard guard(_Scene3D_SpinLock);
	framebuffer->Scene3D_Contexts.append_range(temp);
}

void RenderSystem::processLight(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera)
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
				renderdata->renderCube = renderLight.renderCube;

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

void RenderSystem::processParticle(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera)
{
	auto particleSystem = m_world->getSystem<ParticleSystem>();
	if (!particleSystem)
		return;

	auto& particles = particleSystem->GetParticles();
	std::vector<std::shared_ptr<VKRenderContext::RenderContext>> temp;
	temp.reserve(particles.size());
	for (auto& particle : particles)
	{
		if (!particle || !particle->visible || !particle->properties)
			continue;

		auto renderdata = std::make_shared<VKRenderContext::SceneEffectRenderData>();
		renderdata->properties = particle->properties->Clone();

		auto context = std::make_shared<VKRenderContext::RenderContext>();
		context->type = VKRenderContext::RenderContextType::Effect;
		context->data = renderdata;

		temp.emplace_back(std::move(context));
	}
	LockGuard guard(_Scene3D_SpinLock);
	framebuffer->Scene3D_Contexts.append_range(temp);
}

void RenderSystem::processLaserBeam(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera)
{
	auto views = m_world->getViewWith<LaserBeamEmitter, Transform>();
	std::vector<std::shared_ptr<VKRenderContext::RenderContext>> temp;
	temp.reserve(views.size());
	for (auto [entity, emitter, transform] : views)
	{
		if (!emitter.visible || !emitter.visible || !emitter.properties)
			continue;

		auto renderdata = std::make_shared<VKRenderContext::SceneEffectRenderData>();
		renderdata->properties = emitter.properties;

		auto context = std::make_shared<VKRenderContext::RenderContext>();
		context->type = VKRenderContext::RenderContextType::Effect;
		context->data = renderdata;

		temp.emplace_back(std::move(context));
	}
	LockGuard guard(_Scene3D_SpinLock);
	framebuffer->Scene3D_Contexts.append_range(temp);
}

//void RenderSystem::processFirstPersonWeaponVisual(std::shared_ptr<Render::Render3DFrameData>& framebuffer, Entity& entity)
//{
//	if (!entity.hasComponent<WeaponOwner>())
//		return;
//
//	auto& weaponOwner = entity.getComponent<WeaponOwner>();
//	auto curWeapon = weaponOwner.getCurrentWeapon();
//	if (!curWeapon)
//		return;
//
//	auto* fpsModel = curWeapon.tryGetComponent<FirstPersonRenderModel>();
//	if (!fpsModel || !fpsModel->model)
//		return;
//
//
//	auto renderdata = std::make_shared<VKRenderContext::FirstPersonRenderData>();
//	renderdata->model = fpsModel->model;
//	renderdata->cameraView = fpsModel->cameraView;
//
//	if (auto* aniGroup = curWeapon.tryGetComponent<SkeletonAnimatorGroup>())
//	{
//		bool isAnyAniPlaying = false;
//		for (auto& [name, anidata] : aniGroup->animatorDatas)
//		{
//			if (!anidata || !anidata->animator || !anidata->enable)
//				continue;
//
//			renderdata->animatorViews.push_back(anidata->renderView);
//			isAnyAniPlaying = true;
//		}
//
//		if (!isAnyAniPlaying && aniGroup->ExistAnimator("Static"))
//		{
//			if (auto anidata = aniGroup->GetAnimatorData("Static"))
//			{
//				renderdata->animatorViews.push_back(anidata->renderView);
//			}
//		}
//	}
//
//	auto context = std::make_shared<VKRenderContext::RenderContext>();
//	context->type = VKRenderContext::RenderContextType::FirstPersonModel;
//	context->data = renderdata;
//
//	framebuffer->Scene3D_Contexts.emplace_back(context);
//}

void RenderSystem::processSkybox(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera)
{
	auto view = m_world->getViewWith<SkyBox>();
	if (view.empty())
		return;

	for (auto [entity, skybox] : view)
	{
		if (!skybox.visible || !skybox.cube)
			continue;

		framebuffer->skybox = skybox.cube;
		return;
	}
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


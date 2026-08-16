#include "Render/Systems/RenderSystem.h"
#include "Render/Systems/ParticleSystem.h"
#include "CommonComponent.h"
#include "GameRuntimeComponents.h"
#include "ECSCore/World.h"

using namespace Render;

RenderSystem::RenderSystem()
	:_pool(0)
{
}

void RenderSystem::SetTriBuffer(std::shared_ptr<TripleBuffer<std::shared_ptr<Render::RenderFrameData>>> triBuffer)
{
	_triBuffer = triBuffer;
}

void RenderSystem::SetOpenGLRender(std::shared_ptr<OpenGLRenderer> render)
{
	_render = render;
}

void RenderSystem::postUpdate(float deltaTime)
{
	auto render = _render;
	auto triBuffer = _triBuffer;
	if (!render || !triBuffer)
		return;

	auto bufferframe = triBuffer->acquireWriteBuffer();
	if (!bufferframe)
		return;

	if (!_pool.running())
		_pool.start();

	std::vector<std::shared_ptr<ThreadPool::SubmitHandle<void>>> tasks;

	tasks.push_back(std::move(_pool.submit([&] {processSprite(bufferframe); })));
	tasks.push_back(std::move(_pool.submit([&] {processGIFAnimation(bufferframe); })));
	tasks.push_back(std::move(_pool.submit([&] {processDebugLines(bufferframe); })));

	auto entities = m_world->getEntitiesWith<TagMainCamera, Transform, CameraComponent>();
	if (!entities.empty())
	{
		Entity maincamera = entities[0];

		tasks.push_back(_pool.submit([&] {processModel(bufferframe, maincamera); }));
		tasks.push_back(_pool.submit([&] {processLight(bufferframe, maincamera); }));
		tasks.push_back(_pool.submit([&] {processParticle(bufferframe, maincamera); }));
		tasks.push_back(_pool.submit([&] {processLaserBeam(bufferframe, maincamera); }));
		//tasks.push_back(_pool.submit([&] {processFirstPersonVisual(bufferframe, maincamera); }));
		tasks.push_back(_pool.submit([&] {processSkybox(bufferframe, maincamera); }));
		tasks.push_back(_pool.submit([&] {SyncGLCamera(render, bufferframe, maincamera); }));
	}

	for (auto& task : tasks)
		task->get();

	triBuffer->submitWriteBuffer();
}

void RenderSystem::pushDebugLine(const Line2& line)
{
	_DebugLines.push_back(line);
}

void RenderSystem::processSprite(std::shared_ptr<RenderFrameData>& framebuffer)
{
	auto entities = m_world->getViewWith<Transform2D, Sprite>();

	std::vector<std::shared_ptr<D2DRenderContext::RenderContext>> temp;
	temp.reserve(entities.size());
	for (auto [entity, transform, sprite] : entities)
	{
		if (!sprite.enable ||
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

void RenderSystem::processGIFAnimation(std::shared_ptr<RenderFrameData>& framebuffer)
{
	auto entities = m_world->getViewWith<Transform2D, GIFAnimator>();

	std::vector<std::shared_ptr<D2DRenderContext::RenderContext>> temp;
	temp.reserve(entities.size());
	for (auto [entity, transform, gifanimator] : entities)
	{
		if (!gifanimator.enable || !gifanimator.gifInfo)continue;

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

void RenderSystem::processDebugLines(std::shared_ptr<Render::RenderFrameData>& framebuffer)
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

void RenderSystem::processModel(std::shared_ptr<RenderFrameData>& framebuffer, Entity& maincamera)
{
	auto entities = m_world->getEntitiesWith<Transform, RenderModel>();
	auto& cameraComponent = maincamera.getComponent<CameraComponent>();

	auto view = m_world->getViewWith<Transform, RenderModel>();
	std::vector<std::shared_ptr<OpenGLRenderContext::RenderContext>> temp;
	temp.reserve(entities.size());
	for (auto [entity, transform, renderModel] : view)
	{
		if (!renderModel.enable || !renderModel.model) continue;

		auto renderdata = std::make_shared<OpenGLRenderContext::SceneModelRenderData>();
		auto& writeBuffer = renderModel.renderView.transformTripleBuffer->acquireWriteBuffer();
		writeBuffer = transform.getMatrix() * renderModel.trans;
		renderModel.renderView.transformTripleBuffer->submitWriteBuffer();

		renderdata->transformView = renderModel.renderView;
		renderdata->model = renderModel.model;

		if (entity.hasComponent<VariableMaterial>())
		{
			auto& variableMaterial = entity.getComponent<VariableMaterial>();
			auto& data = variableMaterial.data;
			if (variableMaterial.flags.AnyChange())
			{

				if (variableMaterial.flags.albedoChange)
				{
					for (auto& info : renderModel.model->getMeshInfos())
					{
						auto& material = info.material;
						material->SetAlbedo(variableMaterial.data.albedo);
					}
				}
				if (variableMaterial.flags.metallicChange)
				{
					for (auto& info : renderModel.model->getMeshInfos())
					{
						auto& material = info.material;
						material->SetMetallic(variableMaterial.data.metallic);
					}
				}
				if (variableMaterial.flags.roughnessChange)
				{
					for (auto& info : renderModel.model->getMeshInfos())
					{
						auto& material = info.material;
						material->SetRoughness(variableMaterial.data.roughness);
					}
				}
				if (variableMaterial.flags.opacityChange)
				{
					for (auto& info : renderModel.model->getMeshInfos())
					{
						auto& material = info.material;
						material->SetOpacity(variableMaterial.data.opacity);
					}
				}
				if (variableMaterial.flags.alphamodeChange)
				{
					for (auto& info : renderModel.model->getMeshInfos())
					{
						auto& material = info.material;
						material->SetAlpahMode((AlphaMode)variableMaterial.data.alphamode);
					}
				}
				if (variableMaterial.flags.twosidedChange)
				{
					for (auto& info : renderModel.model->getMeshInfos())
					{
						auto& material = info.material;
						material->SetTwoSided(variableMaterial.data.twosided);
					}
				}

				variableMaterial.flags.Reset();
			}
		}

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

		auto context = std::make_shared<OpenGLRenderContext::RenderContext>();
		context->type = OpenGLRenderContext::RenderContextType::Model;
		context->data = renderdata;

		temp.emplace_back(std::move(context));
	}
	LockGuard guard(_GL_SpinLock);
	framebuffer->GL_Contexts.append_range(temp);
}

void RenderSystem::processLight(std::shared_ptr<Render::RenderFrameData>& framebuffer, Entity& maincamera)
{
	auto views = m_world->getViewWith<Transform, RenderLight>();
	auto& cameraComponent = maincamera.getComponent<CameraComponent>();

	std::vector<std::shared_ptr<OpenGLRenderContext::RenderContext>> temp;
	temp.reserve(views.size());
	for (auto [entity, transform, renderLight] : views)
	{
		if (!renderLight.enable)
			continue;

		switch (renderLight.type)
		{
		case LightType::Directional:
		{
			if (auto data = renderLight.GetData<DirectionalLightData>())
			{
				auto postion = transform.position;
				auto direction = transform.getDirection();

				auto renderdata = std::make_shared<OpenGLRenderContext::DirLightRenderData>();
				renderdata->light = std::make_shared<DirLight>(direction, data->color, data->luxIntensity);
				renderdata->light->setCascadeLevel(data->cascadeLevel);
				renderdata->light->setShadowMapWidth(data->shadowMapWidth);
				renderdata->light->setShadowMapHeight(data->shadowMapHeight);
				renderdata->light->setCastShadow(data->castShadow);
				renderdata->renderCube = renderLight.renderCube;

				auto context = std::make_shared<OpenGLRenderContext::RenderContext>();
				context->type = OpenGLRenderContext::RenderContextType::DirLight;
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
				auto direction = transform.getDirection();

				auto renderdata = std::make_shared<OpenGLRenderContext::PointLightRenderData>();
				renderdata->light = std::make_shared<PointLight>(position, data->color, data->cdIntensity);
				renderdata->light->setShadowMapWidth(data->shadowMapWidth);
				renderdata->light->setShadowMapHeight(data->shadowMapHeight);
				renderdata->light->setCastShadow(data->castShadow);
				renderdata->renderCube = renderLight.renderCube;

				auto context = std::make_shared<OpenGLRenderContext::RenderContext>();
				context->type = OpenGLRenderContext::RenderContextType::PointLight;
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
				auto direction = transform.getDirection();

				auto renderdata = std::make_shared<OpenGLRenderContext::SpotLightRenderData>();
				renderdata->light = std::make_shared<SpotLight>(position, direction, data->cutOffAngle, data->outercutOffAngle, data->color, data->cdIntensity);
				renderdata->light->setShadowMapWidth(data->shadowMapWidth);
				renderdata->light->setShadowMapHeight(data->shadowMapHeight);
				renderdata->light->setCastShadow(data->castShadow);
				renderdata->renderCube = renderLight.renderCube;

				auto context = std::make_shared<OpenGLRenderContext::RenderContext>();
				context->type = OpenGLRenderContext::RenderContextType::SpotLight;
				context->data = renderdata;

				temp.emplace_back(std::move(context));
			}
			break;
		}
		default:
			break;
		}

	}
	LockGuard guard(_GL_SpinLock);
	framebuffer->GL_Contexts.append_range(temp);
}

void RenderSystem::processParticle(std::shared_ptr<Render::RenderFrameData>& framebuffer, Entity& maincamera)
{
	auto particleSystem = m_world->getSystem<ParticleSystem>();
	if (!particleSystem)
		return;

	auto& particles = particleSystem->GetParticles();
	std::vector<std::shared_ptr<OpenGLRenderContext::RenderContext>> temp;
	temp.reserve(particles.size());
	for (auto& particle : particles)
	{
		if (!particle || !particle->enable || !particle->properties)
			continue;

		auto renderdata = std::make_shared<OpenGLRenderContext::SceneEffectRenderData>();
		renderdata->properties = particle->properties->Clone();

		auto context = std::make_shared<OpenGLRenderContext::RenderContext>();
		context->type = OpenGLRenderContext::RenderContextType::Effect;
		context->data = renderdata;

		temp.emplace_back(std::move(context));
	}
	LockGuard guard(_GL_SpinLock);
	framebuffer->GL_Contexts.append_range(temp);
}

void RenderSystem::processLaserBeam(std::shared_ptr<Render::RenderFrameData>& framebuffer, Entity& maincamera)
{
	auto views = m_world->getViewWith<LaserBeamEmitter, Transform>();
	std::vector<std::shared_ptr<OpenGLRenderContext::RenderContext>> temp;
	temp.reserve(views.size());
	for (auto [entity, emitter, transform] : views)
	{
		if (!emitter.enable || !emitter.enable || !emitter.properties)
			continue;

		auto renderdata = std::make_shared<OpenGLRenderContext::SceneEffectRenderData>();
		renderdata->properties = emitter.properties;

		auto context = std::make_shared<OpenGLRenderContext::RenderContext>();
		context->type = OpenGLRenderContext::RenderContextType::Effect;
		context->data = renderdata;

		temp.emplace_back(std::move(context));
	}
	LockGuard guard(_GL_SpinLock);
	framebuffer->GL_Contexts.append_range(temp);
}

//void RenderSystem::processFirstPersonVisual(std::shared_ptr<Render::RenderFrameData>& framebuffer, Entity& maincamera)
//{
//	auto cameraComponent = maincamera.getComponent<CameraComponent>();
//	if (!cameraComponent.isFirstPerson)
//		return;
//
//	auto cameraFollow = maincamera.tryGetComponent<CameraFollow>();
//	if (cameraFollow->target && cameraFollow->target.hasComponents<TagCharacter>())
//	{
//		Entity& character = cameraFollow->target;
//		processFirstPersonWeaponVisual(framebuffer, character);
//	}
//}
//
//void RenderSystem::processFirstPersonWeaponVisual(std::shared_ptr<Render::RenderFrameData>& framebuffer, Entity& entity)
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
//	auto renderdata = std::make_shared<OpenGLRenderContext::FirstPersonRenderData>();
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
//	auto context = std::make_shared<OpenGLRenderContext::RenderContext>();
//	context->type = OpenGLRenderContext::RenderContextType::FirstPersonModel;
//	context->data = renderdata;
//
//	framebuffer->GL_Contexts.emplace_back(context);
//}

void RenderSystem::processSkybox(std::shared_ptr<Render::RenderFrameData>& framebuffer, Entity& maincamera)
{
	auto view = m_world->getViewWith<SkyBox>();
	if (view.empty())
		return;

	for (auto [entity, skybox] : view)
	{
		if (!skybox.enable || !skybox.cube)
			continue;

		framebuffer->skybox = skybox.cube;
		return;
	}
}

void RenderSystem::SyncGLCamera(std::shared_ptr<OpenGLRenderer>& render, std::shared_ptr<RenderFrameData>& framebuffer, Entity& maincamera)
{
	auto& trans = maincamera.getComponent<Transform>();
	auto& camCom = maincamera.getComponent<CameraComponent>();

	float width = render->GetWidth();
	float height = render->GetHeight();
	if (width <= 0) width = 1;
	if (height <= 0) height = 1;

	glm::mat4 projection = camCom.camera.GetPerspectiveProjectionMatrix(width / height);
	glm::mat4 view = camCom.camera.GetViewMatrix();
	glm::vec3 position = camCom.camera.GetPosition();

	framebuffer->projection = projection;
	framebuffer->view = view;
	framebuffer->position = trans.position;
	framebuffer->direction = camCom.camera.GetDirection();
	framebuffer->directionUp = camCom.camera.GetDirectionUp();
	framebuffer->directionRight = camCom.camera.GetDirectionRight();
	framebuffer->nearPlane = camCom.camera.GetNearPlane();
	framebuffer->farPlane = camCom.camera.GetFarPlane();
	framebuffer->fov = camCom.camera.GetFOV();

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


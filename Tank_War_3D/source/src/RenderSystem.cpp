#include "ECS/Systems/RenderSystem.h"
#include "ECS/Components/AllComponent.h"
#include "ECS/Core/World.h"
#include "Manager/RenderManager.h"
#include <format>
#include "Helper/Tools.h"
#include "ECS/Systems/ParticleSystem.h"

using namespace Render;

void RenderSystem::postUpdate(float deltaTime)
{
	auto bufferframe = RenderManager::Instance()->getBufferManager()->acquireWriteBuffer();

	processSprite(bufferframe);
	processGIFAnimation(bufferframe);
	processDebugLines(bufferframe);

	auto entities = m_world->getEntitiesWith<TagMainCamera, Transform, CameraComponent>();
	if (!entities.empty())
	{
		Entity maincamera = entities[0];

		processModel(bufferframe, maincamera);
		processLight(bufferframe, maincamera);
		processParticle(bufferframe, maincamera);
		processLaserBeam(bufferframe, maincamera);
		processFirstPersonVisual(bufferframe, maincamera);

		SyncGLCamera(bufferframe, maincamera);
	}

	RenderManager::Instance()->getBufferManager()->submitWriteBuffer();
}

void RenderSystem::pushDebugLine(const Line2& line)
{
	_DebugLines.push_back(line);
}

void RenderSystem::processSprite(std::shared_ptr<RenderFrameData>& framebuffer)
{
	auto entities = m_world->getEntitiesWith<Transform2D, Sprite>();

	for (auto entity : entities)
	{
		auto& transform = entity.getComponent<Transform2D>();
		auto& sprite = entity.getComponent<Sprite>();

		if (!sprite.bitmap) continue;
		if (sprite.opacity <= 0) continue;
		if (sprite.width <= 0 || sprite.height <= 0) continue;

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

		framebuffer->D2D_Contexts.emplace_back(context);
	}
}

void RenderSystem::processGIFAnimation(std::shared_ptr<RenderFrameData>& framebuffer)
{
	auto entities = m_world->getEntitiesWith<Transform2D, GIFAnimator>();

	for (auto entity : entities)
	{
		auto& transform = entity.getComponent<Transform2D>();
		auto& gifanimator = entity.getComponent<GIFAnimator>();

		if (!gifanimator.gifInfo)continue;
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

		framebuffer->D2D_Contexts.emplace_back(context);
	}
}

void RenderSystem::processDebugLines(std::shared_ptr<Render::RenderFrameData>& framebuffer)
{
	for (auto& line : _DebugLines)
	{
		auto renderdata = std::make_shared <D2DRenderContext::DebugLineRenderData>();
		renderdata->line_pos1 = line.pos1;
		renderdata->line_pos2 = line.pos2;

		auto context = std::make_shared<D2DRenderContext::RenderContext>();
		context->type = D2DRenderContext::RenderContextType::DebugLine;
		context->data = renderdata;

		framebuffer->D2D_Contexts.emplace_back(context);
	}
	_DebugLines.clear();
}

void RenderSystem::processModel(std::shared_ptr<RenderFrameData>& framebuffer, Entity& maincamera)
{
	auto entities = m_world->getEntitiesWith<Transform, RenderModel>();
	auto& cameraComponent = maincamera.getComponent<CameraComponent>();
	auto cameraFollow = maincamera.tryGetComponent<CameraFollow>();


	auto view = m_world->getViewWith<Transform, RenderModel>();
	for (auto [entity, transform, renderModel] : view)
	{
		if (!renderModel.model) continue;

		if (cameraFollow && entity == cameraFollow->target)
		{
			if (cameraComponent.isFirstPerson)
				continue;
		}

		auto renderdata = std::make_shared<OpenGLRenderContext::SceneModelRenderData>();
		auto& writeBuffer = renderModel.renderView.transformTripleBuffer->acquireWriteBuffer();
		writeBuffer = transform.getMatrix() * renderModel.trans;
		renderModel.renderView.transformTripleBuffer->submitWriteBuffer();

		renderdata->transformView = renderModel.renderView;
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

		auto context = std::make_shared<OpenGLRenderContext::RenderContext>();
		context->type = OpenGLRenderContext::RenderContextType::Model;
		context->data = renderdata;

		framebuffer->GL_Contexts.emplace_back(context);
	}

	//for (auto entity : entities)
	//{
	//	auto& transform = entity.getComponent<Transform>();
	//	auto& renderModel = entity.getComponent<RenderModel>();

	//	if (!renderModel.model) continue;

	//	auto renderdata = std::make_shared<OpenGLRenderContext::SceneModelRenderData>();
	//	auto& writeBuffer = renderModel.renderView.transformTripleBuffer->acquireWriteBuffer();
	//	writeBuffer = transform.getMatrix() * renderModel.trans;
	//	renderModel.renderView.transformTripleBuffer->submitWriteBuffer();

	//	renderdata->transformView = renderModel.renderView;
	//	renderdata->model = renderModel.model;

	//	if (cameraFollow && entity == cameraFollow->target)
	//	{
	//		if (cameraComponent.isFirstPerson)
	//			renderdata->isFpsSelfModel = true;
	//	}

	//	if (entity.hasComponent<SkeletonAnimatorGroup>())
	//	{
	//		auto& animatorGroup = entity.getComponent<SkeletonAnimatorGroup>();
	//		for (auto& [name, anidata] : animatorGroup.animatorDatas)
	//		{
	//			if (!anidata || !anidata->animator || !anidata->enable)
	//				continue;
	//			renderdata->animatorViews.push_back(anidata->renderView);
	//		}
	//	}

	//	auto context = std::make_shared<OpenGLRenderContext::RenderContext>();
	//	context->type = OpenGLRenderContext::RenderContextType::Model;
	//	context->data = renderdata;

	//	framebuffer->GL_Contexts.emplace_back(context);
	//}
}

void RenderSystem::processLight(std::shared_ptr<Render::RenderFrameData>& framebuffer, Entity& maincamera)
{
	auto entities = m_world->getEntitiesWith<RenderLight>();
	auto& cameraComponent = maincamera.getComponent<CameraComponent>();
	auto cameraFollow = maincamera.tryGetComponent<CameraFollow>();

	for (auto entity : entities)
	{
		auto& renderLight = entity.getComponent<RenderLight>();

		switch (renderLight.type)
		{
		case LightType::Dir:
		{
			if (renderLight.dirlight)
			{
				auto renderdata = std::make_shared<OpenGLRenderContext::DirLightRenderData>();
				renderdata->light = renderLight.dirlight;
				renderdata->renderCube = renderLight.renderCube;

				auto context = std::make_shared<OpenGLRenderContext::RenderContext>();
				context->type = OpenGLRenderContext::RenderContextType::DirLight;
				context->data = renderdata;

				framebuffer->GL_Contexts.emplace_back(context);
			}
			break;
		}
		case LightType::Point:
		{
			if (renderLight.pointlight)
			{
				auto renderdata = std::make_shared<OpenGLRenderContext::PointLightRenderData>();
				renderdata->light = renderLight.pointlight;
				renderdata->renderCube = renderLight.renderCube;

				auto context = std::make_shared<OpenGLRenderContext::RenderContext>();
				context->type = OpenGLRenderContext::RenderContextType::PointLight;
				context->data = renderdata;

				framebuffer->GL_Contexts.emplace_back(context);
			}
			break;
		}
		case LightType::Spot:
		{
			if (renderLight.spotlight)
			{
				auto renderdata = std::make_shared<OpenGLRenderContext::SpotLightRenderData>();
				renderdata->light = renderLight.spotlight;
				renderdata->renderCube = renderLight.renderCube;

				auto context = std::make_shared<OpenGLRenderContext::RenderContext>();
				context->type = OpenGLRenderContext::RenderContextType::SpotLight;
				context->data = renderdata;

				framebuffer->GL_Contexts.emplace_back(context);
			}
			break;
		}
		default:
			break;
		}
	}
}

void RenderSystem::processParticle(std::shared_ptr<Render::RenderFrameData>& framebuffer, Entity& maincamera)
{
	auto particleSystem = m_world->getSystem<ParticleSystem>();
	if (!particleSystem)
		return;


	auto& particles = particleSystem->GetParticles();
	for (auto& particle : particles)
	{
		if (!particle || !particle->properties)
			continue;

		auto renderdata = std::make_shared<OpenGLRenderContext::SceneEffectRenderData>();
		renderdata->properties = particle->properties->Clone();

		auto context = std::make_shared<OpenGLRenderContext::RenderContext>();
		context->type = OpenGLRenderContext::RenderContextType::Effect;
		context->data = renderdata;

		framebuffer->GL_Contexts.emplace_back(context);
	}
}

void RenderSystem::processLaserBeam(std::shared_ptr<Render::RenderFrameData>& framebuffer, Entity& maincamera)
{
	std::vector<Entity> entities = m_world->getEntitiesWith<LaserBeamEmitter, Transform>();
	for (auto& entity : entities)
	{
		auto& emitter = entity.getComponent<LaserBeamEmitter>();

		if (!emitter.enable || !emitter.properties)
			continue;

		auto renderdata = std::make_shared<OpenGLRenderContext::SceneEffectRenderData>();
		renderdata->properties = emitter.properties;

		auto context = std::make_shared<OpenGLRenderContext::RenderContext>();
		context->type = OpenGLRenderContext::RenderContextType::Effect;
		context->data = renderdata;

		framebuffer->GL_Contexts.emplace_back(context);
	}
}

void RenderSystem::processFirstPersonVisual(std::shared_ptr<Render::RenderFrameData>& framebuffer, Entity& maincamera)
{
	auto cameraComponent = maincamera.getComponent<CameraComponent>();
	if (!cameraComponent.isFirstPerson)
		return;

	auto cameraFollow = maincamera.tryGetComponent<CameraFollow>();
	if (cameraFollow->target && cameraFollow->target.hasComponents<TagCharacter>())
	{
		Entity& character = cameraFollow->target;
		processFirstPersonWeaponVisual(framebuffer, character);
	}
}

void RenderSystem::processFirstPersonWeaponVisual(std::shared_ptr<Render::RenderFrameData>& framebuffer, Entity& entity)
{
	if (!entity.hasComponent<WeaponOwner>())
		return;

	auto& weaponOwner = entity.getComponent<WeaponOwner>();
	auto curWeapon = weaponOwner.getCurrentWeapon();
	if (!curWeapon)
		return;

	auto* fpsModel = curWeapon.tryGetComponent<FirstPersonRenderModel>();
	if (!fpsModel || !fpsModel->model)
		return;


	auto renderdata = std::make_shared<OpenGLRenderContext::FirstPersonRenderData>();
	renderdata->model = fpsModel->model;
	renderdata->cameraView = fpsModel->cameraView;

	if (auto* aniGroup = curWeapon.tryGetComponent<SkeletonAnimatorGroup>())
	{
		bool isAnyAniPlaying = false;
		for (auto& [name, anidata] : aniGroup->animatorDatas)
		{
			if (!anidata || !anidata->animator || !anidata->enable)
				continue;

			renderdata->animatorViews.push_back(anidata->renderView);
			isAnyAniPlaying = true;
		}

		if (!isAnyAniPlaying && aniGroup->ExistAnimator("Static"))
		{
			if (auto anidata = aniGroup->GetAnimatorData("Static"))
			{
				renderdata->animatorViews.push_back(anidata->renderView);
			}
		}
	}

	auto context = std::make_shared<OpenGLRenderContext::RenderContext>();
	context->type = OpenGLRenderContext::RenderContextType::FirstPersonModel;
	context->data = renderdata;

	framebuffer->GL_Contexts.emplace_back(context);
}

void RenderSystem::SyncGLCamera(std::shared_ptr<RenderFrameData>& framebuffer, Entity& maincamera)
{
	auto& trans = maincamera.getComponent<Transform>();
	auto& camCom = maincamera.getComponent<CameraComponent>();

	glm::mat4 projection = camCom.camera.GetPerspectiveProjectionMatrix(
		(float)RenderManager::Instance()->getRenderer()->GetOpenGLWidth() / (float)RenderManager::Instance()->getRenderer()->GetOpenGLHeight());
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


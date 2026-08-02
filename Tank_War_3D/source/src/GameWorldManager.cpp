
#include "Manager/ConnectManager.h"
#include "Manager/GameWorldManager.h"
#include "Manager/UserInfoManager.h"
#include "Manager/RenderContextManager.h"

#include "ECS/Systems/AllSystem.h"
#include "ECS/Components/AllComponent.h"
#include "ECS/Factory/CharacterFactory.h"
#include "ECS/Factory/ParticleEmitterFactory.h"

#include "Helper/DynamicFpsController.h"
#include "Scene.h"
#include "GameDataDef.h"
#include <format>

void SetupDustLight(World& world)
{
	for (int i = 0; i < 1; i++)
	{
		Entity entity = world.createEntityWithTag<TagLight>();
		auto light = std::make_shared<DirLight>(glm::vec3(1, -1, 1));
		light->setShadowMapWidth(3000);
		light->setShadowMapHeight(3000);
		light->setCascadeLevel(4);
		light->setIntensity(3.5);
		auto& renderlight = entity.addComponent<RenderLight>(light);
	}

	for (int i = 0; i < 1; i++)
	{
		Entity entity = world.createEntityWithTag<TagLight>();
		auto light = std::make_shared<SpotLight>(glm::vec3(-15, 5, 144), glm::vec3(0, -1, 0), 45.f, 60.f);
		auto& renderlight = entity.addComponent<RenderLight>(light);
	}

	static std::vector<glm::vec3> innerLightPos = {
		{-45.7,-3.73,11.7},
		{-63.2,-3.73,11.7},
		{-80.5,-3.73,11.7},
		{-97.6,-3.73,11.7},
		{-35.2,-18.2,-6.2},
		{-18.4,-18.2,-6.2}
	};
	for (auto& pos : innerLightPos)
	{
		Entity entity = world.createEntityWithTag<TagLight>();
		auto light = std::make_shared<PointLight>(pos);
		auto& renderlight = entity.addComponent<RenderLight>(light);
	}
}

void CreateTestDustScene(World& world)
{
	if (auto model = ResFactory->GetModelRes(ResName::Keqing1))
	{
		Entity character = CharacterFactory::CreatePlayerCharacter(world, glm::vec3(-35, -7.6, 144), glm::identity<glm::quat>(), model);
		//Entity character = CharacterFactory::CreatePlayerCharacter(world, glm::vec3(-79, -17, -92.5), glm::identity<glm::quat>(), model);
		if (auto* physics = character.tryGetComponent<Physics>())
			physics->walkSpeed = 16.f;

		{
			Entity cameraEntity = world.createEntityWithTag<TagCamera>();
			auto& trans = cameraEntity.addComponent<Transform>();
			auto& cameracom = cameraEntity.addComponent<CameraComponent>();
			cameracom.camera.SetFOV(90.f);
			cameracom.camera.SetNearPlane(0.05f);
			cameracom.camera.SetFarPlane(100.f);
			cameracom.SetTransForm(trans);
			auto& camerafollow = cameraEntity.addComponent<CameraFollow>();
			camerafollow.target = character;
			camerafollow.offset = glm::vec3(0, 2.2, 0.2);
			//world.SetMainCamera(cameraEntity);

			//{
			//	Entity lightEntity = world.createEntityWithTags<TagLight, TagLightShowLight>();
			//	auto& trans = lightEntity.addComponent<Transform>();
			//	auto& light = lightEntity.addComponent<RenderLight>(std::make_shared<SpotLight>(glm::vec3(0, 0, 0), glm::vec3(0, 0, 1), 20, 30));
			//	light.renderCube = false;
			//	light.SetTransForm(trans);
			//	auto& follow = lightEntity.addComponent<LightFollow>();
			//	follow.target = cameraEntity;
			//}
		}
	}

	if (auto model = ResFactory->GetModelRes(ResName::Keqing2))
	{
		Entity character = CharacterFactory::CreatePlayerCharacter(world, glm::vec3(-40, -7.6, 144), glm::identity<glm::quat>(), model);
		if (auto* physics = character.tryGetComponent<Physics>())
			physics->walkSpeed = 20.f;

		//character.addComponent<TagCurrentControl>();

		{
			Entity cameraEntity = world.createEntityWithTag<TagCamera>();
			auto& trans = cameraEntity.addComponent<Transform>();
			auto& cameracom = cameraEntity.addComponent<CameraComponent>();
			cameracom.camera.SetFOV(90.f);
			cameracom.camera.SetNearPlane(0.05f);
			cameracom.camera.SetFarPlane(350.f);
			cameracom.SetTransForm(trans);
			auto& camerafollow = cameraEntity.addComponent<CameraFollow>();
			camerafollow.target = character;
			camerafollow.offset = glm::vec3(0, 2.2, 0.2);
			//world.SetMainCamera(cameraEntity);
		}
	}

	{
		Entity freeEntity = world.createEntityWithTag<TagFreeCamera>();
		freeEntity.addComponent<Transform>();
		freeEntity.addComponent<PlayerInput>();
		auto& controller = freeEntity.addComponent<Controller>();
		controller.yaw = -90.f;
		controller.pitch = 0.f;
		freeEntity.addComponent<TagCurrentControl>();

		{
			Entity cameraEntity = world.createEntityWithTag<TagCamera>();
			auto& trans = cameraEntity.addComponent<Transform>(glm::vec3(-15, -7.6, 144));
			auto& cameracom = cameraEntity.addComponent<CameraComponent>();
			cameracom.camera.SetFOV(90.f);
			cameracom.camera.SetNearPlane(0.1f);
			cameracom.camera.SetFarPlane(350.f);
			cameracom.SetTransForm(trans);
			auto& camerafollow = cameraEntity.addComponent<CameraFollow>();
			camerafollow.target = freeEntity;
			camerafollow.offset = glm::vec3(0, 2.8, 0);
			world.SetMainCamera(cameraEntity);

			//if (freeEntity.hasComponent<TagCurrentControl>() && cameraEntity.hasComponent<TagMainCamera>())
			//{
			//	Entity lightEntity = world.createEntityWithTags<TagLight, TagLightShowLight>();
			//	auto& trans = lightEntity.addComponent<Transform>();
			//	auto& light = lightEntity.addComponent<RenderLight>(std::make_shared<SpotLight>(glm::vec3(0, 0, 0), glm::vec3(0, 0, 1), 20, 30));
			//	light.renderCube = false;
			//	light.SetTransForm(trans);
			//	auto& follow = lightEntity.addComponent<LightFollow>();
			//	follow.target = freeEntity;
			//}
		}
	}

	if (auto quad = ResFactory->GetModelRes(ResName::Quad))
	{
		auto model = quad->Clone();
		float floorSize = 40.f;
		model->MakeScale(glm::vec3(floorSize));

		Entity floor = world.createEntity();
		auto& trans = floor.addComponent<Transform>();
		trans.position = glm::vec3(23.5, -16.6, 29);
		glm::mat4 mat = glm::rotate(glm::mat4(1.0f), glm::radians(90.f), glm::vec3(0, 0, 1));
		trans.rotation = glm::quat_cast(mat);

		AABB aabb = model->GetAABB();
		float SizeX = aabb.max.x - aabb.min.x;
		float SizeZ = aabb.max.z - aabb.min.z;

		trans.scale = glm::vec3(10.f / SizeX, 1.f, 30.f / SizeZ);

		auto& physics = floor.addComponent<Physics>();
		physics.bodyType = Physics::BodyType::Static;
		physics.isSensor = false;
		physics.isBullet = true;
		physics.collisionShape.AddBoxShape(glm::vec3(5.f, 0.4f, 15.f));

		auto& rendermodel = floor.addComponent<RenderModel>();
		rendermodel.model = model;
		rendermodel.trans = glm::translate(rendermodel.trans, glm::vec3(0.f, 0.4f, 0.f));
		//rendermodel.trans = glm::scale(rendermodel.trans, glm::vec3(10 / SizeX, 1, 10 / SizeZ));

		if (!model->getMeshInfos().empty() && model->getMeshInfos()[0].material)
		{
			auto& material = model->getMeshInfos()[0].material;
			material->SetAlbedo(glm::vec3(224.f / 255.f));
			material->SetMetallic(0.9);
			material->SetRoughness(0.05f);
		}
	}

	{

		Entity dust2 = world.createEntity();
		auto& trans = dust2.addComponent<Transform>();
		trans.position = glm::vec3(0, 0.f, 0);
		trans.rotation = glm::identity<glm::quat>();

		glm::vec3 scale = glm::vec3(1.0f);

		if (auto model = ResFactory->GetModelRes(ResName::NewDust2))
		{
			auto& physics = dust2.addComponent<Physics>();
			physics.bodyType = Physics::BodyType::Static;
			physics.isSensor = false;
			physics.isBullet = true;
			for (auto& info : model->getMeshInfos())
			{
				auto& mesh = info.mesh;
				if (!mesh)continue;
				std::vector<glm::vec3> vertices;
				for (auto& vertex : mesh->GetVertices())
					vertices.emplace_back(vertex.Position.x, vertex.Position.y, vertex.Position.z);
				for (auto& v : vertices)
					v *= scale;
				physics.collisionShape.AddTriangleMeshShape(vertices, mesh->GetIndices());
			}

			auto& rendermodel = dust2.addComponent<RenderModel>();
			rendermodel.model = model;
			rendermodel.trans = glm::translate(rendermodel.trans, glm::vec3(0, 0, 0));
			rendermodel.trans = glm::scale(rendermodel.trans, scale);
		}
	}

	if (auto model = ResFactory->GetModelRes(ResName::AK47))
	{
		Entity testAnimation = world.createEntity();
		auto& trans = testAnimation.addComponent<Transform>(glm::vec3(-20, -7.6, 144), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(0.03));
		auto& renderModel = testAnimation.addComponent<RenderModel>(model);
		if (auto animation = ResFactory->GetAnimationRes(ResName::AK47_Anim_Reload_Full))
		{
			auto& aniGroup = testAnimation.addComponent<SkeletonAnimatorGroup>();
			aniGroup.AddAnimator("Reload", animation, renderModel.model->GetSkeleton(), true);
		}
	}

	SetupDustLight(world);
}

void SetupeTestGameLight(World& world)
{
	//{
	//	Entity entity = world.createEntityWithTag<TagLight>();
	//	auto light = std::make_shared<DirLight>(glm::vec3(1, -1, 1));
	//	light->setShadowMapWidth(3000);
	//	light->setShadowMapHeight(3000);
	//	light->setCascadeLevel(4);
	//	light->setIntensity(2.5);
	//	auto& renderlight = entity.addComponent<RenderLight>(light);
	//}

	{
		Entity entity = world.createEntityWithTags<TagLight>();
		auto light = std::make_shared<PointLight>(glm::vec3(0, 10, 100));
		light->setIntensity(300.f);
		light->setColorTemperature(25000);
		auto& renderlight = entity.addComponent<RenderLight>(light);
	}

	//{
	//	Entity entity = world.createEntityWithTag<TagLight>();
	//	auto light = std::make_shared<PointLight>(glm::vec3(10, 5, 100));
	//	light->setColor(1.0f, 0, 0.0f);
	//	auto& renderlight = entity.addComponent<RenderLight>(light);
	//}

	//{
	//	Entity entity = world.createEntityWithTag<TagLight>();
	//	auto light = std::make_shared<PointLight>(glm::vec3(0, 15, 100));
	//	light->setColor(0, 1, 0);
	//	auto& renderlight = entity.addComponent<RenderLight>(light);
	//}

	//{
	//	Entity entity = world.createEntityWithTag<TagLight>();
	//	auto light = std::make_shared<PointLight>(glm::vec3(0, 5, 110));
	//	light->setColor(0, 0, 1);
	//	auto& renderlight = entity.addComponent<RenderLight>(light);
	//}

	//for (int i = 0; i < 1; i++)
	//{
	//	Entity entity = world.createEntityWithTag<TagLight>();
	//	auto light = std::make_shared<DirLight>(glm::vec3(-1, -1, -1));
	//	light->setAmbientStrength(1.0f);
	//	light->setDiffuseStrength(1.0f);
	//	light->setSpecularStrength(1.0f);
	//	light->setShadowMapWidth(2048);
	//	light->setShadowMapHeight(2048);
	//	light->setCascadeLevel(4);
	//	auto& renderlight = entity.addComponent<RenderLight>(light);
	//}
}

void CreateTestGameScene(World& world)
{

	{

		Entity freeEntity = world.createEntityWithTag<TagFreeCamera>();
		freeEntity.addComponent<Transform>();
		freeEntity.addComponent<PlayerInput>();
		auto& controller = freeEntity.addComponent<Controller>();
		controller.yaw = 90.f;
		controller.pitch = 0.f;
		freeEntity.addComponent<TagCurrentControl>();


		{
			Entity cameraEntity = world.createEntityWithTag<TagCamera>();
			auto& trans = cameraEntity.addComponent<Transform>(glm::vec3(0, 5, 77));
			auto& cameracom = cameraEntity.addComponent<CameraComponent>();
			cameracom.camera.SetFOV(90.f);
			cameracom.camera.SetNearPlane(0.05f);
			cameracom.camera.SetFarPlane(350.f);
			cameracom.SetTransForm(trans);
			auto& camerafollow = cameraEntity.addComponent<CameraFollow>();
			camerafollow.target = freeEntity;
			camerafollow.offset = glm::vec3(0, 2.8, 0);
			world.SetMainCamera(cameraEntity);

			//if (freeEntity.hasComponent<TagCurrentControl>() && cameraEntity.hasComponent<TagMainCamera>())
			//{
			//	Entity lightEntity = world.createEntityWithTags<TagLight, TagLightShowLight>();
			//	auto& trans = lightEntity.addComponent<Transform>();
			//	auto& light = lightEntity.addComponent<RenderLight>(std::make_shared<SpotLight>(glm::vec3(0, 0, 0), glm::vec3(0, 0, 1), 20, 30));
			//	light.renderCube = false;
			//	light.SetTransForm(trans);
			//	auto& follow = lightEntity.addComponent<LightFollow>();
			//	follow.target = freeEntity;
			//}
		}
	}

	if (auto model = ResFactory->GetModelRes(ResName::Keqing2))
	{
		Entity character = CharacterFactory::CreatePlayerCharacter(world, glm::vec3(5, 3, 100), glm::identity<glm::quat>(), model);
		auto& aniGroup = character.addComponent<SkeletonAnimatorGroup>();
		auto& controller = character.getComponent<Controller>();
		controller.yaw = -90.f;
		controller.pitch = 0.f;
		auto& physics = character.getComponent<Physics>();
		physics.walkSpeed = 15.f;
		physics.mass = 45.f;
		//character.addComponent<TagCurrentControl>();

		{
			Entity cameraEntity = world.createEntityWithTag<TagCamera>();
			auto& trans = cameraEntity.addComponent<Transform>();
			auto& cameracom = cameraEntity.addComponent<CameraComponent>();
			cameracom.camera.SetFOV(90.f);
			cameracom.camera.SetNearPlane(0.05f);
			cameracom.camera.SetFarPlane(350.f);
			cameracom.SetTransForm(trans);
			auto& camerafollow = cameraEntity.addComponent<CameraFollow>();
			camerafollow.target = character;
			camerafollow.offset = glm::vec3(0, 2.5, 0.2);
			//world.SetMainCamera(cameraEntity);

			//{
			//	Entity lightEntity = world.createEntityWithTags<TagLight, TagLightShowLight>();
			//	auto& trans = lightEntity.addComponent<Transform>();
			//	auto& light = lightEntity.addComponent<RenderLight>(std::make_shared<SpotLight>(glm::vec3(0, 0, 0), glm::vec3(0, 0, 1), 20, 30));
			//	light.renderCube = false;
			//	light.SetTransForm(trans);
			//	auto& follow = lightEntity.addComponent<LightFollow>();
			//	follow.target = cameraEntity;
			//}
		}
	}

	//if (auto model = ResFactory->GetModelRes(ResName::lowPolyForest))
	if (auto model = std::shared_ptr<Model>())
	{
		Entity forest = world.createEntity();
		auto& trans = forest.addComponent<Transform>();
		trans.position = glm::vec3(0, -30.f, 30);
		trans.rotation = glm::identity<glm::quat>();

		auto& physics = forest.addComponent<Physics>();
		physics.bodyType = Physics::BodyType::Static;
		physics.isSensor = false;
		physics.isBullet = true;
		for (auto& info : model->getMeshInfos())
		{
			auto& mesh = info.mesh;
			if (!mesh)continue;
			std::vector<glm::vec3> vertices;
			for (auto& vertex : mesh->GetVertices())
				vertices.emplace_back(vertex.Position.x, vertex.Position.y, vertex.Position.z);
			physics.collisionShape.AddTriangleMeshShape(vertices, mesh->GetIndices());
		}

		auto& rendermodel = forest.addComponent<RenderModel>();
		rendermodel.model = model;
	}

	for (int i = 0; i < 2; i++)
	{
		float floorSize = 40.f;
		auto model = ResFactory->GetModelRes(ResName::Quad)->Clone();
		model->MakeScale(glm::vec3(floorSize));

		auto& meshInfo = model->getMeshInfos();
		if (!meshInfo.empty())
			//meshInfo[0].material->SetBaseColor(glm::vec3(51, 255, 153) / 255.f);
			meshInfo[0].material->SetAlbedo(glm::vec3(1, 0, 0));


		AABB aabb = model->GetAABB();
		float Half_SizeX = (aabb.max.x - aabb.min.x) / 2.f;
		float Half_SizeZ = (aabb.max.z - aabb.min.z) / 2.f;

		float half_width = 10;
		float half_height = Half_SizeZ;

		Entity floor = world.createEntity();
		auto& trans = floor.addComponent<Transform>();
		trans.position = glm::vec3(i == 0 ? Half_SizeX : -Half_SizeX, half_width, 100);
		trans.rotation = Tool::getQuatFromRotate(i == 0 ? 90 : -90, glm::vec3(0, 0, 1));
		trans.scale = glm::vec3(half_width / Half_SizeX, 1.0f, half_height / Half_SizeZ);

		auto& physics = floor.addComponent<Physics>();
		physics.bodyType = Physics::BodyType::Static;
		physics.isSensor = false;
		physics.isBullet = true;
		physics.collisionShape.AddBoxShape(glm::vec3(half_width, 0.2f, half_height));

		auto& rendermodel = floor.addComponent<RenderModel>();
		rendermodel.model = model;

		//if (!model->getMeshInfos().empty() && model->getMeshInfos()[0].material)
		//{
		//	auto& material = model->getMeshInfos()[0].material;
		//	material->SetMetallic(0.9);
		//}
	}

	for (int i = 0; i < 1; i++)
	{
		float floorSize = 40.f;
		auto model = ResFactory->GetModelRes(ResName::Quad)->Clone();
		model->MakeScale(glm::vec3(floorSize));

		auto& meshInfo = model->getMeshInfos();
		if (!meshInfo.empty())
			//meshInfo[0].material->SetBaseColor(glm::vec3(51, 255, 153) / 255.f);
			meshInfo[0].material->SetAlbedo(glm::vec3(0, 0, 1));

		AABB aabb = model->GetAABB();
		float Half_SizeX = (aabb.max.x - aabb.min.x) / 2.f;
		float Half_SizeZ = (aabb.max.z - aabb.min.z) / 2.f;

		float half_width = Half_SizeX;
		float half_height = 10;

		Entity floor = world.createEntity();
		auto& trans = floor.addComponent<Transform>();
		trans.position = glm::vec3(0, half_height, 100 + (i == 0 ? Half_SizeZ : -Half_SizeZ));
		trans.rotation = Tool::getQuatFromRotate(i == 0 ? -90 : 90, glm::vec3(1, 0, 0));
		trans.scale = glm::vec3(half_width / Half_SizeX, 1.0f, half_height / Half_SizeZ);

		auto& physics = floor.addComponent<Physics>();
		physics.bodyType = Physics::BodyType::Static;
		physics.isSensor = false;
		physics.isBullet = true;
		physics.collisionShape.AddBoxShape(glm::vec3(half_width, 0.2f, half_height));

		auto& rendermodel = floor.addComponent<RenderModel>();
		rendermodel.model = model;

		if (!model->getMeshInfos().empty() && model->getMeshInfos()[0].material)
		{
			auto& material = model->getMeshInfos()[0].material;
			//material->SetMetallic(0.9);
			//material->SetRoughness(0.0f);
		}
	}

	for (int i = 0; i < 1; i++)
	{
		float floorSize = 40.f;
		auto model = ResFactory->GetModelRes(ResName::Quad)->Clone();
		model->MakeScale(glm::vec3(floorSize));

		auto& meshInfo = model->getMeshInfos();
		if (!meshInfo.empty())
			//meshInfo[0].material->SetBaseColor(glm::vec3(51, 255, 153) / 255.f);
			meshInfo[0].material->SetAlbedo(glm::vec3(1, 1, 1));

		AABB aabb = model->GetAABB();
		float Half_SizeX = (aabb.max.x - aabb.min.x) / 2.f;
		float Half_SizeZ = (aabb.max.z - aabb.min.z) / 2.f;

		Entity floor = world.createEntity();
		auto& trans = floor.addComponent<Transform>();
		trans.position = glm::vec3(0, i == 0 ? 0 : Half_SizeX, 100);
		trans.rotation = i == 0 ? glm::identity<glm::quat>() : Tool::getQuatFromRotate(180.f, glm::vec3(1, 0, 0));

		auto& physics = floor.addComponent<Physics>();
		physics.bodyType = Physics::BodyType::Static;
		physics.isSensor = false;
		physics.isBullet = true;
		physics.collisionShape.AddBoxShape(glm::vec3(Half_SizeX, 0.2f, Half_SizeZ), glm::vec3(0, -0.2, 0));

		auto& rendermodel = floor.addComponent<RenderModel>();
		rendermodel.model = model;

		//if (i == 0)
		//{
		//	if (!model->getMeshInfos().empty() && model->getMeshInfos()[0].material)
		//	{
		//		auto& material = model->getMeshInfos()[0].material;
		//		material->SetAlbedo(glm::vec3(0.85, 0.87, 0.89));
		//		material->SetMetallic(0.93);
		//		material->SetRoughness(0.05f);
		//	}
		//}
	}

	struct CubeInfo
	{
		glm::vec3 pos = glm::vec3(0);
		glm::vec3 scale = glm::vec3(1);
		glm::quat rotation = glm::identity<glm::quat>();
		glm::vec3 color = glm::vec3(1);
	};

	std::vector<CubeInfo> cubeInfos = {
		{{5,3,90},{1,1,1},Tool::getQuatFromRotate(45.f, glm::vec3(0,0,1)), glm::vec3(1,1,1)},
		{{-5,1,70},{2,0.3,3},Tool::getQuatFromRotate(-25.f, glm::vec3(0,0,1)),glm::vec3(1,0,0)},
		{{5,1,60},{2,0.3,3},Tool::getQuatFromRotate(25.f, glm::vec3(0,0,1)), glm::vec3(1,1,0)},
		{{0,1,45},{10,0.3,5},Tool::getQuatFromRotate(0.f, glm::vec3(0,0,1)), glm::vec3(0,1,0)},
		{{-10,3,100},{1,1,1},Tool::getQuatFromRotate(45.f, glm::vec3(0,0,1)), glm::vec3(1,1,0)},
		{{-5,3,90},{1,1,1},Tool::getQuatFromRotate(0.f, glm::vec3(0,0,1)), glm::vec3(0,1,1)},
		{{10,3,100},{1,1,1},Tool::getQuatFromRotate(0.f, glm::vec3(0,0,1)), glm::vec3(0,0,1)}
	};

	if (auto model = ResFactory->GetModelRes(ResName::Cube))
	{
		for (auto& cubeinfo : cubeInfos)
		{
			Entity cube_1 = world.createEntity();
			auto& trans = cube_1.addComponent<Transform>();
			trans.position = cubeinfo.pos;
			trans.rotation = cubeinfo.rotation;
			trans.scale = cubeinfo.scale;

			auto& physics = cube_1.addComponent<Physics>();
			physics.bodyType = Physics::BodyType::Static;
			physics.isSensor = false;
			physics.isBullet = true;
			physics.collisionShape.AddBoxShape(glm::vec3(trans.scale));

			auto& rendermodel = cube_1.addComponent<RenderModel>();
			rendermodel.model = model->Clone();
			auto& meshInfo = rendermodel.model->getMeshInfos();
			if (!meshInfo.empty())
				meshInfo[0].material->SetAlbedo(cubeinfo.color);

			//if (!model->getMeshInfos().empty() && model->getMeshInfos()[0].material)
			//{
			//	auto& material = model->getMeshInfos()[0].material;
			//	material->SetMetallic(0.6);
			//	material->SetRoughness(0.1f);
			//}

			//cube_1.addComponent<LightShow>();
		}
	}

	if (auto model = ResFactory->GetModelRes(ResName::Sphere))
	{
		float radius = 4.2;

		Entity sphere = world.createEntity();
		auto& trans = sphere.addComponent<Transform>();
		trans.position = { -7,10,95 };
		trans.scale = glm::vec3(radius / 0.5f);

		auto& physics = sphere.addComponent<Physics>();
		physics.bodyType = Physics::BodyType::Dynamic;
		physics.isSensor = false;
		physics.isBullet = true;
		physics.friction = 0.1;
		physics.restitution = 0.8;
		physics.mass = 5.f;
		physics.collisionShape.AddSphereShape(radius);

		auto& rendermodel = sphere.addComponent<RenderModel>();
		rendermodel.model = model->Clone();
	}

	if (auto model = ResFactory->GetModelRes(ResName::Sphere))
	{
		float radius = 4.2;

		Entity sphere = world.createEntity();
		auto& trans = sphere.addComponent<Transform>();
		trans.position = { -3,10,105 };
		trans.scale = glm::vec3(radius / 0.5f);

		auto& physics = sphere.addComponent<Physics>();
		physics.bodyType = Physics::BodyType::Dynamic;
		physics.isSensor = false;
		physics.isBullet = true;
		physics.friction = 0.1;
		physics.restitution = 0.8;
		physics.mass = 5.f;
		physics.collisionShape.AddSphereShape(radius);

		auto& rendermodel = sphere.addComponent<RenderModel>();
		rendermodel.model = model->Clone();
	}

	if (auto model = ResFactory->GetModelRes(ResName::Cylinder))
	{
		float radius = 1;
		float height = 18;

		Entity sphere = world.createEntity();
		auto& trans = sphere.addComponent<Transform>();
		trans.position = { 7,3,95 };
		trans.scale = glm::vec3(radius / 0.5f, height, radius / 0.5f);
		//trans.rotation = Tool::getQuatFromRotate(90.f, { 1,1,0 });

		auto& physics = sphere.addComponent<Physics>();
		physics.bodyType = Physics::BodyType::Dynamic;
		physics.isSensor = false;
		physics.isBullet = true;
		physics.mass = 5.f;
		physics.friction = 0.1;
		physics.restitution = 0.8;
		physics.collisionShape.AddCylinderShape(radius, height);

		auto& rendermodel = sphere.addComponent<RenderModel>();
		rendermodel.model = model->Clone();
		auto& meshInfo = rendermodel.model->getMeshInfos();

		//if (!model->getMeshInfos().empty() && model->getMeshInfos()[0].material)
		//{
		//	auto& material = model->getMeshInfos()[0].material;
		//	material->SetMetallic(0.6);
		//	material->SetRoughness(0.1f);
		//}
	}

	{
		auto fireEmitter = ParticleEmitterFactory::CreateFireEmitter(world, glm::vec3(-12, 5, 106), Tool::getQuatFromRotate(45.f, glm::vec3(1, 0, 0)));
	}

	//{
	//	auto vortexEmitter = ParticleEmitterFactory::CreateVortexEmitter(world, glm::vec3(-5, 150, -150), Tool::getQuatFromRotate(180.f, glm::vec3(1, 0, 0)));
	//}

	//if (auto model = ResFactory->GetModelRes(ResName::Sphere))
	//{
	//	auto vortexEmitter = ParticleEmitterFactory::CreateTestEmitter(world, glm::vec3(-5, 0, -150), Tool::getQuatFromRotate(180.f, glm::vec3(1, 0, 0)), model);
	//}

	{
		Entity laserEmitterEntity = world.createEntity();

		auto& trans = laserEmitterEntity.addComponent<Transform>();
		trans.position = glm::vec3(-10, 3, 110);
		//trans.rotation = Tool::getQuatFromRotate(180.f, glm::vec3(0, 1, 0));

		auto& emitter = laserEmitterEntity.addComponent<LaserBeamEmitter>(glm::vec3(1, 0.2, 0));
		//emitter.properties->white_width = 1.f;
		//emitter.properties->color_width = 1.f;
	}

	SetupeTestGameLight(world);
}

void CreateTestSponzaScene(World& world)
{

	{

		Entity freeEntity = world.createEntityWithTag<TagFreeCamera>();
		auto trans = freeEntity.addComponent<Transform>();
		freeEntity.addComponent<PlayerInput>();
		auto& controller = freeEntity.addComponent<Controller>();
		controller.yaw = 0.f;
		controller.pitch = 0.f;
		freeEntity.addComponent<TagCurrentControl>();

		auto& tag = freeEntity.getComponent<TagFreeCamera>();
		tag.velocity = 0.08;

		{
			Entity cameraEntity = world.createEntityWithTag<TagCamera>();
			auto& trans = cameraEntity.addComponent<Transform>(glm::vec3(0, 5, 100));
			trans.position = glm::vec3(-32.7, 3.8, 0.7);
			auto& cameracom = cameraEntity.addComponent<CameraComponent>();
			cameracom.camera.SetFOV(90.f);
			cameracom.camera.SetNearPlane(0.05f);
			cameracom.camera.SetFarPlane(350.f);
			cameracom.SetTransForm(trans);
			auto& camerafollow = cameraEntity.addComponent<CameraFollow>();
			camerafollow.target = freeEntity;
			camerafollow.offset = glm::vec3(0, 0, 0);
			world.SetMainCamera(cameraEntity);

			//if (freeEntity.hasComponent<TagCurrentControl>() && cameraEntity.hasComponent<TagMainCamera>())
			//{
			//	Entity lightEntity = world.createEntityWithTags<TagLight, TagLightShowLight>();
			//	auto& trans = lightEntity.addComponent<Transform>();
			//	auto& light = lightEntity.addComponent<RenderLight>(std::make_shared<SpotLight>(glm::vec3(0, 0, 0), glm::vec3(0, 0, 1), 20, 30));
			//	light.renderCube = false;
			//	light.SetTransForm(trans);
			//	auto& follow = lightEntity.addComponent<LightFollow>();
			//	follow.target = freeEntity;
			//}
		}
	}


	if (auto model = ResFactory->GetModelRes(ResName::Sponza))
	{

		Entity dust2 = world.createEntity();
		auto& trans = dust2.addComponent<Transform>();
		trans.position = glm::vec3(0, 0.f, 0);
		trans.rotation = glm::identity<glm::quat>();

		auto& physics = dust2.addComponent<Physics>();
		physics.bodyType = Physics::BodyType::Static;
		physics.isSensor = false;
		physics.isBullet = true;
		for (auto& info : model->getMeshInfos())
		{
			auto& mesh = info.mesh;
			if (!mesh)continue;
			std::vector<glm::vec3> vertices;
			for (auto& vertex : mesh->GetVertices())
				vertices.emplace_back(vertex.Position.x, vertex.Position.y, vertex.Position.z);
			physics.collisionShape.AddTriangleMeshShape(vertices, mesh->GetIndices());
		}

		auto& rendermodel = dust2.addComponent<RenderModel>();
		rendermodel.model = model;
	}


	for (int i = 0; i < 1; i++)
	{
		Entity entity = world.createEntityWithTag<TagLight>();
		auto light = std::make_shared<DirLight>(glm::vec3(-0.4, -1, 0.35));
		light->setShadowMapWidth(3000);
		light->setShadowMapHeight(3000);
		light->setIntensity(4);
		light->setCascadeLevel(4);
		auto& renderlight = entity.addComponent<RenderLight>(light);
		renderlight.renderCube = true;
	}

	{
		//Entity entity = world.createEntityWithTag<TagLight>();
		//auto light = std::make_shared<PointLight>(glm::vec3(0, 25, 0));
		//auto& renderlight = entity.addComponent<RenderLight>(light);
	}
}

void LoadLocalGameMapInfoToWorld(std::shared_ptr<World>& world, const MapInfo& mapinfo)
{
	CreateTestDustScene(*world);
	//CreateTestGameScene(*world);
	//CreateTestSponzaScene(*world);
}


GameWorldManager* GameWorldManager::Instance()
{
	static GameWorldManager* m_instance = new GameWorldManager();
	return m_instance;
}

void GameWorldManager::SyncFromServerState(const json& js)
{
	//if (!_world)
	//	return;

	//auto& world = _world;

	//if (auto sync = world->getSystem<ClientSyncSystem>())
	//{
	//	if (js.contains("gamestate") && js["gamestate"].is_object())
	//	{
	//		GameState gamestate = GameState::fromJson(js["gamestate"]);
	//		sync->InputGameState(gamestate);
	//	}
	//}
}

void GameWorldManager::SyncFromServerEvent(const json& js)
{
	//if (!_world)
	//	return;

	//auto& world = _world;

	//if (auto sync = world->getSystem<ClientSyncSystem>())
	//{
	//	if (js.contains("event") && js["event"].is_object())
	//	{
	//		SyncEvent event = SyncEvent::fromJson(js["event"]);
	//		sync->InputEvent(event);
	//	}
	//}
}

void GameWorldManager::ProcessEliminateInfo(const json& js)
{
	if (!js.contains("gameid") || !js["gameid"].is_string())
		return;
	if (!js.contains("playerid") || !js["playerid"].is_string())
		return;
	if (!js.contains("killerDescription") || !js["killerDescription"].is_string())
		return;

	GameID gameid = js.value("gameid", "");
	PlayerID playerid = js.value("playerid", "");
	std::string killerDescription = js.value("killerDescription", "");

	if (gameid == UserInfoManager::Instance()->gameid() && playerid == UserInfoManager::Instance()->usertoken())
	{
		std::wstring formatstring;
		if (killerDescription.empty())
		{
			formatstring = std::format(L"您已经被淘汰！");
		}
		else
		{
			formatstring = std::format(L"您已经被{}淘汰！", Tool::UTF8ToWString(killerDescription));
		}
		//CreateTooltip(_hwnd, NULL, formatstring.c_str());
	}
}

void GameWorldManager::ProcessGameOver(const json& js)
{
	StopWorld();
	if (!js.contains("gameid") || !js["gameid"].is_string())
		return;
	if (!js.contains("winnerid") || !js["winnerid"].is_string())
		return;

	GameID gameid = js.value("gameid", "");
	PlayerID winnerid = js.value("winnerid", "");


	if (gameid == UserInfoManager::Instance()->gameid())
	{
		if (UserInfoManager::Instance()->isMyToken(winnerid))
		{
			PostMessage(RENDERCONTEXMANAGER->GetHwnd(), WM_COMMAND, WIN, (LPARAM)RENDERCONTEXMANAGER->GetHwnd());
		}
		else
		{
			PostMessage(RENDERCONTEXMANAGER->GetHwnd(), WM_COMMAND, FAIL, (LPARAM)RENDERCONTEXMANAGER->GetHwnd());
		}
	}
}

GameWorldManager::GameWorldManager()
	:_stop(true)
{
}

void GameWorldManager::InitGameWorld(GameMode mode, MapID mapid)
{
	_world = std::make_shared<World>();

	if (mode == GameMode::RunGame)
	{
		// 通过World注册系统
		auto& inputSystem = _world->registerSystem<LocalInputSystem>();
		auto& movementSystem = _world->registerSystem<MovementSystem>();
		auto& renderSystem = _world->registerSystem<RenderSystem>();
		auto& lifetimeSystem = _world->registerSystem<LifetimeSystem>();
		auto& physicsSystem = _world->registerSystem<PhysicsSystem>();
		auto& destroySystem = _world->registerSystem<DestroySystem>();
		auto& healthSystem = _world->registerSystem<HealthSystem>();
		auto& audioSystem = _world->registerSystem<AudioSystem>();
		auto& cameraFollowSystem = _world->registerSystem<CameraFollowSystem>();
		auto& weaponSystem = _world->registerSystem<WeaponSystem>();
		auto& animationSystem = _world->registerSystem<AnimationSystem>();
		auto& mapBoundarySystem = _world->registerSystem<MapBoundarySystem>();
		auto& lightShowSystem = _world->registerSystem<LightShowSystem>();
		auto& lightFollowSystem = _world->registerSystem<LightFollowSystem>();
		auto& particleSystem = _world->registerSystem<ParticleSystem>();
		auto& laserBeamSystem = _world->registerSystem<LaserBeamSystem>();

		mapBoundarySystem.SetBoundaryHeight(-60.f);

		lifetimeSystem.setPriority(10000);
		inputSystem.setPriority(1000);
		movementSystem.setPriority(500);
		weaponSystem.setPriority(400);
		mapBoundarySystem.setPriority(350);
		physicsSystem.setPriority(300);
		laserBeamSystem.setPriority(250);
		particleSystem.setPriority(200);
		cameraFollowSystem.setPriority(-3000);
		lightFollowSystem.setPriority(-3100);
		destroySystem.setPriority(-8000);
		animationSystem.setPriority(-9000);
		renderSystem.setPriority(-10000);

		auto mapinfo = MapManager::Instance()->getMap(mapid);
		LoadLocalGameMapInfoToWorld(_world, mapinfo);

		//Entity map_boundary = _world->createEntityWithTag<TagGameBoundary>();
		//auto& boundary = map_boundary.addComponent<BoundaryPhysisc>(MapBoundary::left, MapBoundary::top, MapBoundary::right, MapBoundary::bottom);
	}
	else if (mode == GameMode::MapEdit)
	{
	}
}

void GameWorldManager::InitOnlineGameWorld()
{
	//_world = std::make_shared<World>();


	//// 通过World注册系统
	//auto& inputSystem = _world->registerSystem<ClientInputSystem>();
	//auto& velocityControlSystem = _world->registerSystem<VelocityControlSystem>();
	////auto& movementSystem = _world->registerSystem<MovementSystem>();
	////auto& weaponSystem = _world->registerSystem<WeaponSystem>();
	//auto& bulletSystem = _world->registerSystem<BulletSystem>();
	//auto& renderSystem = _world->registerSystem<RenderSystem>();
	//auto& lifetimeSystem = _world->registerSystem<LifetimeSystem>();
	//auto& predictSystem = _world->registerSystem<PredictionSystem>();
	//auto& healthSystem = _world->registerSystem<HealthSystem>();
	//auto& effectSystem = _world->registerSystem<EffectSystem>();
	//auto& wallSystem = _world->registerSystem<WallSystem>();
	//auto& propSystem = _world->registerSystem<PropSystem>();
	//auto& tankSystem = _world->registerSystem<TankSystem>();
	//auto& syncSystem = _world->registerSystem<ClientSyncSystem>();
	//auto& interpolationSystem = _world->registerSystem<InterpolationSystem>();
	//auto& audioSystem = _world->registerSystem<AudioSystem>();


	//syncSystem.setPriority(20000);
	//lifetimeSystem.setPriority(10000);
	//inputSystem.setPriority(1000);
	//velocityControlSystem.setPriority(500);
	////movementSystem.setPriority(500);
	//interpolationSystem.setPriority(400);
	//predictSystem.setPriority(300);
	//renderSystem.setPriority(-10000);

	////auto mapinfo = MapManager::Instance()->getMap(0);
	//LoadLocalGameMapInfoToWorld(_world, MapInfo{ .backGrounp_resname = ResName::sandBK });

	//Entity map_boundary = _world->createEntityWithTag<TagGameBoundary>();
	//auto& boundary = map_boundary.addComponent<BoundaryPhysisc>(MapBoundary::left, MapBoundary::top, MapBoundary::right, MapBoundary::bottom);
}

std::shared_ptr<World> GameWorldManager::GetGameWorld()
{
	return _world;
}


void GameWorldManager::RunWorld()
{
	if (!_world || _world->isRunning())
		return;

	// 启动世界
	_world->setLogicDeltaTime(1000.f / 165.f);
	_world->setFixedDeltaTime(1000.f / 60.f);
	_world->start();
	_stop = false;
	_worldThread = std::make_shared<std::thread>(&GameWorldManager::WorldLoop, this);
}

void GameWorldManager::WorldLoop()
{
	int targetfps = (1000.f / std::min(_world->getFixedDeltaTime(), _world->getLogicDeltaTime())) + 1;
	DynamicFpsController fpscontroller(targetfps);
	fpscontroller.reset();

	// 主循环
	while (!_stop)
	{
		float dt = fpscontroller.getTimeDiffMS();

		_world->update(dt);

		fpscontroller.run();
	}
}

void GameWorldManager::PauseWorld()
{
	_stop = true;
	if (_worldThread)
	{
		if (_worldThread->joinable())
			_worldThread->join();
		_worldThread.reset();
	}
}

void GameWorldManager::StopWorld()
{
	_stop = true;
	if (_worldThread)
	{
		if (_worldThread->joinable())
			_worldThread->join();
		_worldThread.reset();
	}
	_world.reset();
}

std::shared_ptr<std::thread> GameWorldManager::GetWorldThread()
{
	return _worldThread;
}

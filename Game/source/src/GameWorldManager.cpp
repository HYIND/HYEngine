
#include "Manager/ConnectManager.h"
#include "Manager/GameWorldManager.h"
#include "Manager/UserInfoManager.h"
#include "Manager/ResourceManager.h"

#include "CommonComponent.h"
#include "GamePlayComponents.h"
#include "GameRuntimeComponents.h"

#include "CommonSystems.h"
#include "GamePlaySystems.h"
#include "GameRuntimeSystems.h"

#include "Factory/LightFactory.h"
#include "Factory/CharacterFactory.h"
#include "Factory/ParticleEmitterFactory.h"

#include "Helper/DynamicFpsController.h"
#include "Scene.h"
#include "GameDataDef.h"
#include <format>

#include "GeneralManager/FocusManager.h"
#include "GeneralManager/RenderManager.h"
void HandleKeyboardShortcuts()
{
	if (!FocusManager::Instance()->ShouldProcessInput())
		return;

	RenderOption option = RenderManager::Instance()->GetOption();

	static bool s_rayTraceEnable = option.flags.rayTraceOn;
	static bool s_ssrOn = option.flags.ssrOn;
	static bool s_useGbuffer = option.rayTraceParams.useGbuffer;
	static bool s_ssgiOn = option.flags.ssgiOn;

	bool change = false;
	static auto GetCharVK = [](char c)->int
		{
			c = std::toupper(c);
			return 0x41 + (c - 'A');
		};
	if ((GetAsyncKeyState(GetCharVK('B')) & 0x8000))
	{
		static int64_t lasttime = 0;
		auto now = Tool::GetTimestampMilliseconds();
		if ((now - lasttime) > 500)
		{
			s_ssrOn = !s_ssrOn;
			std::cout << std::format("SSR {}\n", s_ssrOn ? "ONNNNNNNNNNNNNNNN" : "OFFFFFFFFFFFFFFF");
			lasttime = now;
			change = true;
		}
	}
	if ((GetAsyncKeyState(GetCharVK('N')) & 0x8000))
	{
		static int64_t lasttime = 0;
		auto now = Tool::GetTimestampMilliseconds();
		if ((now - lasttime) > 500)
		{
			s_rayTraceEnable = !s_rayTraceEnable;
			std::cout << std::format("RayTrace {}\n", s_rayTraceEnable ? "ONNNNNNNNNNNNNNNN" : "OFFFFFFFFFFFFFFF");
			lasttime = now;
			change = true;
		}
	}
	if ((GetAsyncKeyState(GetCharVK('L')) & 0x8000))
	{
		static int64_t lasttime = 0;
		auto now = Tool::GetTimestampMilliseconds();
		if ((now - lasttime) > 500)
		{
			s_useGbuffer = !s_useGbuffer;
			std::cout << std::format("RayTrace useGbuffer {}\n", s_useGbuffer ? "ONNNNNNNNNNNNNNNN" : "OFFFFFFFFFFFFFFF");
			lasttime = now;
			change = true;
		}
	}
	if ((GetAsyncKeyState(GetCharVK('H')) & 0x8000))
	{
		static int64_t lasttime = 0;
		auto now = Tool::GetTimestampMilliseconds();
		if ((now - lasttime) > 500)
		{
			s_ssgiOn = !s_ssgiOn;
			std::cout << std::format("SSGI {}\n", s_ssgiOn ? "ONNNNNNNNNNNNNNNN" : "OFFFFFFFFFFFFFFF");
			lasttime = now;
			change = true;
		}
	}

	if (change)
	{
		if (s_rayTraceEnable)
			s_ssrOn = false;

		option.flags.rayTraceOn = s_rayTraceEnable;
		option.flags.ssrOn = s_ssrOn;
		option.rayTraceParams.useGbuffer = s_useGbuffer;
		option.flags.ssgiOn = s_ssgiOn;
		RenderManager::Instance()->SetOption(option);
	}
}

void SetupTestWeapon(World& world, Entity& entity)
{

	struct SkeletonAnimationData {
		std::string name;
		std::shared_ptr<Animation> ani;
		bool initenable = false;
	};
	static auto setupWeaponAnimatorGroup = [](Entity weapon, std::shared_ptr<Model> model, const std::vector<SkeletonAnimationData>& datas)-> void
		{
			if (!model) return;

			auto& fpsModel = weapon.addComponent<FirstPersonRenderModel>();
			fpsModel.model = model;

			auto& aniGroup = weapon.addComponent<SkeletonAnimatorGroup>();
			if (auto skeleton = model->GetSkeleton())
			{
				for (auto& data : datas)
					aniGroup.AddAnimator(data.name, data.ani, skeleton, data.initenable);
			}

			for (auto& data : datas)
			{
				if (data.ani)
				{
					glm::mat4 trans = glm::mat4(1.0f);
					if (data.ani->TryGetCameraTransform(trans))
					{
						glm::vec3 position = glm::vec3(trans[3]);
						fpsModel.cameraView = glm::lookAt(position, position + glm::vec3(0, 0, 1), glm::vec3(0, 1, 0));
					}
					else
						fpsModel.cameraView = glm::lookAt(glm::vec3(0), glm::vec3(0, 0, 1), glm::vec3(0, 1, 0));

					break;
				}
			}
		};

	auto& weaponOwner = entity.addComponent<WeaponOwner>();

	for (int i = 0; i < 4; i++)
	{
		Entity weapon = world.createEntityWithTag<TagWeapon>();
		auto& weaponBaseic = weapon.addComponent<WeaponBasic>();
		weaponBaseic.weaponName = std::format("weapon_{}", i);
		auto& weaponState = weapon.addComponent<WeaponState>(weaponBaseic.maxAmmo, weaponBaseic.maxReserveAmmo);

		if (i == 0)
		{
			std::vector<SkeletonAnimationData> datas = {
				{"Static", ResFactory->GetAnimationRes(ResName::AK47_Anim_Static)},
				{"Draw", ResFactory->GetAnimationRes(ResName::AK47_Anim_Draw)},
				{"Idle", ResFactory->GetAnimationRes(ResName::AK47_Anim_Idle), true},
				{"Reload", ResFactory->GetAnimationRes(ResName::AK47_Anim_Reload_Full)},
				{"Run", ResFactory->GetAnimationRes(ResName::AK47_Anim_Run)},
				{"Shot", ResFactory->GetAnimationRes(ResName::AK47_Anim_Shot)},
				{"Walk", ResFactory->GetAnimationRes(ResName::AK47_Anim_Walk)}
			};
			setupWeaponAnimatorGroup(weapon, ResFactory->GetModelRes(ResName::AK47), datas);
		}

		if (i == 1)
		{
			std::vector<SkeletonAnimationData> datas = {
				{"Static", ResFactory->GetAnimationRes(ResName::Pistol_Anim_Static),		},
				{"Draw", ResFactory->GetAnimationRes(ResName::Pistol_Anim_Draw),			},
				{"Idle", ResFactory->GetAnimationRes(ResName::Pistol_Anim_Idle),		true},
				{"Reload", ResFactory->GetAnimationRes(ResName::Pistol_Anim_Reload_Full),	},
				{"Run", ResFactory->GetAnimationRes(ResName::Pistol_Anim_Run),				},
				{"Shot", ResFactory->GetAnimationRes(ResName::Pistol_Anim_Shot),			},
				{"Walk", ResFactory->GetAnimationRes(ResName::Pistol_Anim_Walk),			}
			};
			setupWeaponAnimatorGroup(weapon, ResFactory->GetModelRes(ResName::Pistol), datas);
		}

		if (i == 2)
		{
			std::vector<SkeletonAnimationData> datas = {
				{"Static", ResFactory->GetAnimationRes(ResName::Sniper_Anim_Static),		},
				{"Draw", ResFactory->GetAnimationRes(ResName::Sniper_Anim_Draw),			},
				{"Idle", ResFactory->GetAnimationRes(ResName::Sniper_Anim_Idle),		true},
				{"Reload", ResFactory->GetAnimationRes(ResName::Sniper_Anim_Reload_Full),	},
				{"Run", ResFactory->GetAnimationRes(ResName::Sniper_Anim_Run),				},
				{"Shot", ResFactory->GetAnimationRes(ResName::Sniper_Anim_Shot),			},
				{"Walk", ResFactory->GetAnimationRes(ResName::Sniper_Anim_Walk),			}
			};
			setupWeaponAnimatorGroup(weapon, ResFactory->GetModelRes(ResName::Sniper), datas);
		}

		if (i == 3)
		{
			std::vector<SkeletonAnimationData> datas = {
				{"Static", ResFactory->GetAnimationRes(ResName::SMG_Anim_Static),		},
				{"Draw", ResFactory->GetAnimationRes(ResName::SMG_Anim_Draw),			},
				{"Idle", ResFactory->GetAnimationRes(ResName::SMG_Anim_Idle),		true},
				{"Reload", ResFactory->GetAnimationRes(ResName::SMG_Anim_Reload_Full),	},
				{"Run", ResFactory->GetAnimationRes(ResName::SMG_Anim_Run),				},
				{"Shot", ResFactory->GetAnimationRes(ResName::SMG_Anim_Shot),			},
				{"Walk", ResFactory->GetAnimationRes(ResName::SMG_Anim_Walk),			}
			};
			setupWeaponAnimatorGroup(weapon, ResFactory->GetModelRes(ResName::SMG), datas);
		}

		weaponOwner.weapons.push_back(weapon);
	}
	weaponOwner.currentIndex = 0;
}

void SetupDustLight(World& world)
{
	for (int i = 0; i < 1; i++)
	{
		Entity entity = LightFactory::CreateDirLight(world, glm::vec3(1, -1, 1), glm::vec3(1.0f), 3.5f, true, 4, 3000, 3000);
		auto& renderlight = entity.getComponent<RenderLight>();
		renderlight.renderCube = true;
	}

	for (int i = 0; i < 1; i++)
	{
		Entity entity = LightFactory::CreateSpotLight(world, glm::vec3(-15, 5, 144), glm::vec3(0, -1, 0), 300.f, 45.f, 60.f);
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
		Entity entity = LightFactory::CreatePointLight(world, pos);
	}
}

void CreateTestDustScene(World& world)
{
	if (auto cube = ResFactory->GetTextureCubeRes(ResName::skybox1))
	{
		Entity entity = world.createEntityWithTag<TagSkyBox>();
		entity.addComponent<SkyBox>(cube);
	}

	{
		Entity freeEntity = world.createEntityWithTag<TagFreeCamera>();
		freeEntity.addComponent<Transform>();
		freeEntity.addComponent<PlayerInput>();
		auto& controller = freeEntity.addComponent<Controller>();
		controller.yaw = -90.f;
		controller.pitch = 0.f;
		freeEntity.addComponent<TagCurrentControl>();
		auto& tag = freeEntity.getComponent<TagFreeCamera>();
		tag.velocity = 0.5;

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

		//CharacterFactory::CreatePlayerCharacter(world, glm::vec3(-30, -7.6, 144), glm::identity<glm::quat>(), model);
	}

	if (auto quad = ResFactory->GetModelRes(ResName::Quad))
	{
		auto model = quad->Clone();
		float floorSize = 40.f;

		Entity floor = world.createEntity();
		auto& trans = floor.addComponent<Transform>();
		trans.position = glm::vec3(23.5, -16.6, 29);
		glm::mat4 mat = glm::rotate(glm::mat4(1.0f), glm::radians(90.f), glm::vec3(0, 0, 1));
		trans.rotation = glm::quat_cast(mat);

		AABB aabb = model->GetAABB();
		float SizeX = aabb.max.x - aabb.min.x;
		float SizeZ = aabb.max.z - aabb.min.z;

		trans.scale = glm::vec3(10.f / SizeX, 0.4f, 30.f / SizeZ);

		auto& physics = floor.addComponent<Physics>();
		physics.bodyType = Physics::BodyType::Static;
		physics.isSensor = false;
		physics.isBullet = true;
		physics.collisionShape.AddBoxShape();

		auto& rendermodel = floor.addComponent<RenderModel>();
		rendermodel.model = model;
		rendermodel.trans = glm::translate(rendermodel.trans, glm::vec3(0.f, 0.f, 0.f));
		//rendermodel.trans = glm::scale(rendermodel.trans, glm::vec3(10 / SizeX, 1, 10 / SizeZ));

		if (!model->getMeshInfos().empty() && model->getMeshInfos()[0].material)
		{
			auto& material = model->getMeshInfos()[0].material;
			material->SetAlbedo(glm::vec3(224.f / 255.f));
			material->SetMetallic(0.9);
			material->SetRoughness(0.05f);
		}
	}

	if (auto model = ResFactory->GetModelRes(ResName::NewDust2))
	{

		Entity scene = world.createEntity();
		auto& trans = scene.addComponent<Transform>();
		trans.position = glm::vec3(0, 0.f, 0);
		trans.rotation = glm::identity<glm::quat>();

		auto& physics = scene.addComponent<Physics>();
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

		auto& rendermodel = scene.addComponent<RenderModel>();
		rendermodel.model = model;

	}

	if (auto model = ResFactory->GetModelRes(ResName::AK47))
	{
		Entity testAnimation = world.createEntity();
		auto& trans = testAnimation.addComponent<Transform>(glm::vec3(-20, -7.6, 144), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(0.03));
		auto& renderModel = testAnimation.addComponent<RenderModel>(model->Clone(false, true, true));

		auto& aniGroup = testAnimation.addComponent<SkeletonAnimatorGroup>();
		if (auto animation = ResFactory->GetAnimationRes(ResName::AK47_Anim_Reload_Full))
			aniGroup.AddAnimator("Reload", animation, renderModel.model->GetSkeleton(), true);
		if (auto animation = ResFactory->GetAnimationRes(ResName::AK47_Anim_Static))
			aniGroup.AddAnimator("Static", animation, renderModel.model->GetSkeleton(), false);
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
		Entity entity = LightFactory::CreatePointLight(world, glm::vec3(0, 10, 100), 300.f, Tool::ColorTemperatureToRGB(25000));
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
	if (auto cube = ResFactory->GetTextureCubeRes(ResName::skybox2))
	{
		Entity entity = world.createEntityWithTag<TagSkyBox>();
		entity.addComponent<SkyBox>(cube);
	}

	{

		Entity freeEntity = world.createEntityWithTag<TagFreeCamera>();
		freeEntity.addComponent<Transform>();
		freeEntity.addComponent<PlayerInput>();
		auto& controller = freeEntity.addComponent<Controller>();
		controller.yaw = 90.f;
		controller.pitch = 0.f;
		freeEntity.addComponent<TagCurrentControl>();
		auto& tag = freeEntity.getComponent<TagFreeCamera>();
		tag.velocity = 0.25;

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

			if (freeEntity.hasComponent<TagCurrentControl>() && cameraEntity.hasComponent<TagMainCamera>())
			{
				auto light = LightFactory::CreateSpotLight(world, glm::vec3(0, 0, 0), glm::vec3(0, 0, 1), 100.f, 17.5, 30);
				light.addComponent<TagLightShowLight>();
				auto& follow = light.addComponent<LightFollow>();
				follow.target = cameraEntity;
			}
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
			//	auto light = LightFactory::CreateSpotLight(world, glm::vec3(0, 0, 0), glm::vec3(0, 0, 1), 300.f, 20, 30);
			//	light.addComponent<TagLightShowLight>();
			//	auto& follow = light.addComponent<LightFollow>();
			//	follow.target = cameraEntity;
			//}
		}

		SetupTestWeapon(world, character);
	}

	for (int i = 0; i < 2; i++)
	{
		float floorSize = 40.f;
		auto model = ResFactory->GetModelRes(ResName::Quad)->Clone(true, true, false);

		auto& meshInfo = model->getMeshInfos();
		if (!meshInfo.empty())
			//meshInfo[0].material->SetBaseColor(glm::vec3(51, 255, 153) / 255.f);
			meshInfo[0].material->SetAlbedo(glm::vec3(1, 0, 0));

		AABB aabb = model->GetAABB();
		float Half_SizeX = (aabb.max.x - aabb.min.x) / 2.f;
		float Half_SizeZ = (aabb.max.z - aabb.min.z) / 2.f;

		float target_half_width = floorSize / 4.f;
		float target_half_height = floorSize / 2.f;

		Entity floor = world.createEntity();
		auto& trans = floor.addComponent<Transform>();
		trans.position = glm::vec3(i == 0 ? target_half_height : -target_half_height, target_half_width, 100);
		trans.rotation = Tool::GetQuatFromRotate(i == 0 ? 90 : -90, glm::vec3(0, 0, 1));
		trans.scale = glm::vec3(target_half_width / Half_SizeX, 1.0f, target_half_height / Half_SizeZ);

		auto& physics = floor.addComponent<Physics>();
		physics.bodyType = Physics::BodyType::Static;
		physics.isSensor = false;
		physics.isBullet = true;
		physics.collisionShape.AddBoxShape(glm::vec3(Half_SizeX, 0.2f, Half_SizeZ));

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
		auto model = ResFactory->GetModelRes(ResName::Quad)->Clone(true, true, false);

		auto& meshInfo = model->getMeshInfos();
		if (!meshInfo.empty())
			//meshInfo[0].material->SetBaseColor(glm::vec3(51, 255, 153) / 255.f);
			meshInfo[0].material->SetAlbedo(glm::vec3(0, 0, 1));

		AABB aabb = model->GetAABB();
		float Half_SizeX = (aabb.max.x - aabb.min.x) / 2.f;
		float Half_SizeZ = (aabb.max.z - aabb.min.z) / 2.f;

		float target_half_width = floorSize / 2.f;
		float target_half_height = floorSize / 4.f;

		Entity floor = world.createEntity();
		auto& trans = floor.addComponent<Transform>();
		trans.position = glm::vec3(0, target_half_height, 100 + (i == 0 ? target_half_width : -target_half_width));
		trans.rotation = Tool::GetQuatFromRotate(i == 0 ? -90 : 90, glm::vec3(1, 0, 0));
		trans.scale = glm::vec3(target_half_width / Half_SizeX, 1.0f, target_half_height / Half_SizeZ);

		auto& physics = floor.addComponent<Physics>();
		physics.bodyType = Physics::BodyType::Static;
		physics.isSensor = false;
		physics.isBullet = true;
		physics.collisionShape.AddBoxShape(glm::vec3(Half_SizeX, 0.2f, Half_SizeZ));

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
		auto model = ResFactory->GetModelRes(ResName::Quad)->Clone(true, true, false);

		auto& meshInfo = model->getMeshInfos();
		if (!meshInfo.empty())
			meshInfo[0].material->SetAlbedo(glm::vec3(1, 1, 1));

		AABB aabb = model->GetAABB();
		float Half_SizeX = (aabb.max.x - aabb.min.x) / 2.f;
		float Half_SizeZ = (aabb.max.z - aabb.min.z) / 2.f;

		float target_half_width = floorSize / 2.f;
		float target_half_height = floorSize / 2.f;

		Entity floor = world.createEntity();
		auto& trans = floor.addComponent<Transform>();
		trans.position = glm::vec3(0, i == 0 ? 0 : target_half_width, 100);
		trans.rotation = i == 0 ? glm::identity<glm::quat>() : Tool::GetQuatFromRotate(180.f, glm::vec3(1, 0, 0));
		trans.scale = glm::vec3(target_half_width / Half_SizeX, 1.0, target_half_height / Half_SizeZ);

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
		{{5,3,90},{1,1,1},Tool::GetQuatFromRotate(45.f, glm::vec3(0,0,1)), glm::vec3(1,1,1)},
		{{-5,1,70},{2,0.3,3},Tool::GetQuatFromRotate(-25.f, glm::vec3(0,0,1)),glm::vec3(1,0,0)},
		{{5,1,60},{2,0.3,3},Tool::GetQuatFromRotate(25.f, glm::vec3(0,0,1)), glm::vec3(1,1,0)},
		{{0,1,45},{10,0.3,5},Tool::GetQuatFromRotate(0.f, glm::vec3(0,0,1)), glm::vec3(0,1,0)},
		{{-10,3,100},{1,1,1},Tool::GetQuatFromRotate(45.f, glm::vec3(0,0,1)), glm::vec3(1,1,0)},
		{{-5,3,90},{1,1,1},Tool::GetQuatFromRotate(0.f, glm::vec3(0,0,1)), glm::vec3(0,1,1)},
		{{10,3,100},{1,1,1},Tool::GetQuatFromRotate(0.f, glm::vec3(0,0,1)), glm::vec3(0,0,1)}
	};

	if (auto orimodel = ResFactory->GetModelRes(ResName::Cube))
	{
		for (auto& cubeinfo : cubeInfos)
		{
			Entity cube_1 = world.createEntity();
			auto& trans = cube_1.addComponent<Transform>();
			trans.position = cubeinfo.pos;
			trans.rotation = cubeinfo.rotation;
			trans.scale = cubeinfo.scale;

			auto aabbExtend = orimodel->GetAABB().max - orimodel->GetAABB().min;

			auto& physics = cube_1.addComponent<Physics>();
			physics.bodyType = Physics::BodyType::Static;
			physics.isSensor = false;
			physics.isBullet = true;
			physics.collisionShape.AddBoxShape();

			auto model = orimodel->Clone({ false,true,false });
			auto& rendermodel = cube_1.addComponent<RenderModel>();
			rendermodel.model = model;

			auto& meshInfo = rendermodel.model->getMeshInfos();
			if (!meshInfo.empty())
				meshInfo[0].material->SetAlbedo(cubeinfo.color);

			//if (!model->getMeshInfos().empty() && model->getMeshInfos()[0].material)
			//{
			//	auto& material = model->getMeshInfos()[0].material;
			//	material->SetMetallic(0.6);
			//	material->SetRoughness(0.1f);
			//}

			cube_1.addComponent<LightShow>();
			cube_1.addComponent<TagLightShowEntity>();
			cube_1.addComponent<VariableMaterial>();
			cube_1.addComponent<ModelProxy>(std::make_shared<ModelProxyData>());
		}
	}

	if (auto orimodel = ResFactory->GetModelRes(ResName::Sphere))
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
		physics.collisionShape.AddSphereShape();

		auto model = orimodel->Clone(false, true, false);

		auto& rendermodel = sphere.addComponent<RenderModel>();
		rendermodel.model = model;

		if (!model->getMeshInfos().empty() && model->getMeshInfos()[0].material)
		{
			auto& material = model->getMeshInfos()[0].material;
			material->SetMetallic(0.75);
			material->SetRoughness(0.1f);
			material->SetAlbedo(glm::vec3(0.8f));
		}
	}

	if (auto orimodel = ResFactory->GetModelRes(ResName::Sphere))
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
		physics.collisionShape.AddSphereShape();

		auto model = orimodel->Clone(false, true, false);

		auto& rendermodel = sphere.addComponent<RenderModel>();
		rendermodel.model = model;

		if (!model->getMeshInfos().empty() && model->getMeshInfos()[0].material)
		{
			auto& material = model->getMeshInfos()[0].material;
			material->SetMetallic(0.75);
			material->SetRoughness(0.1f);
			material->SetAlbedo(glm::vec3(0.8f));
		}
	}

	if (auto model = ResFactory->GetModelRes(ResName::Cylinder))
	{
		float radius = 1;
		float height = 18;

		Entity sphere = world.createEntity();
		auto& trans = sphere.addComponent<Transform>();
		trans.position = { 7,10,95 };
		trans.scale = glm::vec3(radius / 0.5f, height, radius / 0.5f);
		//trans.rotation = Tool::GetQuatFromRotate(90.f, { 1,1,0 });

		auto& physics = sphere.addComponent<Physics>();
		physics.bodyType = Physics::BodyType::Dynamic;
		physics.isSensor = false;
		physics.isBullet = true;
		physics.mass = 5.f;
		physics.collisionShape.AddCylinderShape();

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
		auto fireEmitter = ParticleEmitterFactory::CreateFireEmitter(world, glm::vec3(-12, 5, 106), Tool::GetQuatFromRotate(0.f, glm::vec3(1, 0, 0)));
		auto& audioSource = fireEmitter.addComponent<AudioSource>();
		audioSource.info = ResFactory->GetAudioRes(ResName::FireAudio);
		audioSource.volume = 0.5f;
		audioSource.isLooping = false;
		audioSource.Play();
	}

	//{
	//	auto vortexEmitter = ParticleEmitterFactory::CreateVortexEmitter(world, glm::vec3(-5, 150, -150), Tool::GetQuatFromRotate(180.f, glm::vec3(1, 0, 0)));
	//}

	//if (auto model = ResFactory->GetModelRes(ResName::Sphere))
	//{
	//	auto vortexEmitter = ParticleEmitterFactory::CreateTestEmitter(world, glm::vec3(-5, 0, -150), Tool::GetQuatFromRotate(180.f, glm::vec3(1, 0, 0)), model);
	//}

	{
		Entity laserEmitterEntity = world.createEntity();

		auto& trans = laserEmitterEntity.addComponent<Transform>();
		trans.position = glm::vec3(-10, 3, 110);
		trans.rotation = Tool::GetQuatFromRotate(180.f, glm::vec3(0, 1, 0));

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

		Entity scene = world.createEntity();
		auto& trans = scene.addComponent<Transform>();
		trans.position = glm::vec3(0, 0.f, 0);
		trans.rotation = glm::identity<glm::quat>();

		auto& physics = scene.addComponent<Physics>();
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

		auto& rendermodel = scene.addComponent<RenderModel>();
		rendermodel.model = model;
	}


	for (int i = 0; i < 1; i++)
	{
		Entity entity = LightFactory::CreateDirLight(world, glm::vec3(-0.4, -1, 0.35), glm::vec3(1.0f), 3.5f, true, 4, 3000, 3000);
		auto& renderlight = entity.getComponent<RenderLight>();
		renderlight.renderCube = true;
	}

	{
		//Entity entity = world.createEntityWithTag<TagLight>();
		//auto light = std::make_shared<PointLight>(glm::vec3(0, 25, 0));
		//auto& renderlight = entity.addComponent<RenderLight>(light);
	}
}

void LoadLocalGameMapInfoToWorld(std::shared_ptr<World>& world)
{
	//CreateTestDustScene(*world);
	CreateTestGameScene(*world);
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

void GameWorldManager::InitGameWorld()
{
	_world = std::make_shared<World>();


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

	renderSystem.SetTriBuffer(RenderManager::Instance()->getBufferManager());
	renderSystem.SetOpenGLRender(RenderManager::Instance()->getRenderer()->GetOpenGLRender());

	LoadLocalGameMapInfoToWorld(_world);

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

		HandleKeyboardShortcuts();
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

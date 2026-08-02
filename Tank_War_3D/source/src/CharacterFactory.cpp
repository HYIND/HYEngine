#include "ECS/Factory/CharacterFactory.h"

Entity CharacterFactory::CreatePlayerCharacter(World& world, const glm::vec3& position, const glm::quat& rotation, std::shared_ptr<Model> model)
{
	Entity character = world.createEntityWithTag<TagCharacter>();
	character.addComponent<TagPlayer>();

	auto& trans = character.addComponent<Transform>();
	trans.position = position;
	trans.rotation = rotation;

	character.addComponent<PlayerInput>();
	auto& controller = character.addComponent<Controller>();
	character.addComponent<CharacterMovement>();
	character.addComponent<Health>();

	auto& rendermodel = character.addComponent<RenderModel>();
	rendermodel.model = model;

	AABB aabb = rendermodel.model->GetAABB();
	rendermodel.trans = glm::translate(rendermodel.trans, glm::vec3(0, -((aabb.min.y + aabb.max.y) / 2), 0));

	float height = aabb.max.y - aabb.min.y;
	float radius = std::max(aabb.max.x - aabb.min.x, aabb.max.z - aabb.min.z) / 2.f;

	auto& physics = character.addComponent<Physics>();
	physics.bodyType = Physics::BodyType::Kinematic;
	physics.isSensor = false;
	physics.isBullet = true;

	physics.isCharacter = true;
	physics.walkSpeed = 10.f;
	physics.jumpSpeed = 5.f;

	physics.collisionShape.AddCapsuleShape(radius, std::max(0.f, height - radius * 2));

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

	auto& weaponOwner = character.addComponent<WeaponOwner>();

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

	return character;
}

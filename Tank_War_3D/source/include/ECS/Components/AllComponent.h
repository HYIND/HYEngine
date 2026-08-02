#pragma once

// ================ 基础组件 ================
#include "Transform.h"          // 变换（位置、旋转、缩放）
#include "Transform2D.h"        // 2D变换
#include "RenderBase.h"         // 可渲染组件基类
#include "LifeTime.h"			// 生命周期

// ================ 渲染组件 ================
#include "Sprite.h"
#include "GIFAnimator.h"
#include "RenderModel.h"
#include "RenderLight.h"
#include "SkeletonAnimator.h"		//骨骼动画
#include "ParticleEmitter.h"
#include "LaserBeamEmitter.h"

// ================ 物理组件 ================
#include "Movement.h"           // 移动控制
#include "Physics.h"            // 物理碰撞

// ================ 游戏实体属性 ================
#include "Health.h"             // 生命值

// ================ 输入与控制 ================
#include "PlayerInput.h"        // 玩家输入
#include "Controller.h"         // 控制器（意图）

// ================ 标签组件（多为空结构体） ================
#include "Tags.h"               // 实体标签（Tank, Wall等）

// ================ 相机 ================
#include "CameraComponent.h"
#include "CameraFollow.h"

// ================ 武器 ================
#include "WeaponBasic.h"
#include "WeaponState.h"
#include "WeaponOwner.h"

// ================ 在线游戏 ================

// ================ 玩法 ================
#include "LightShow.h"

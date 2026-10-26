#pragma once

// ================ 基础组件 ================
#include "Components/Transform.h"			// 变换（位置、旋转、缩放）
#include "Components/LifeTime.h"			// 生命周期
#include "Components/Controller.h"			// 基础控制
#include "Components/Transform2D.h"			// 2D变换
#include "Components/Script.h"				// Lua脚本引用
#include "Components/UIView.h"				// UI 文档（屏幕空间控件树）

// ================ 渲染基础 ===============
#include "Components/Renderable.h"

// ================ 渲染数据/代理 ===============
#include "Components/ModelProxy.h"
#include "Components/RenderLight.h"

// ================ 物理组件 ================
#include "Components/Movement.h"
#include "Components/Physics.h"            // 物理碰撞

// ================ 相机 ===============
#include "Components/CameraComponent.h"     // 相机组件
#include "Components/CameraFollow.h"        // 相机跟随

// ================ 标签组件（多为空结构体） ================
#include "CommonTags.h"

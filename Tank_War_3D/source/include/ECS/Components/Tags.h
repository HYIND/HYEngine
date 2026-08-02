#pragma once

#include "ECS/Core/IComponent.h"

struct TagPlayer :public IComponent {};   // 坦克
struct TagCharacter :public IComponent {};   // 人物
struct TagBullet :public IComponent {}; // 子弹
struct TagWall :public IComponent {};   // 墙
struct TagProp :public IComponent {};	 // 道具


struct TagPhysiscCreate :public IComponent {};	//标记是否已创建物理关联
struct TagGameBoundary :public IComponent {};	//标记游戏边界实体

struct TagEffect :public IComponent {};  //效果标记

struct TagDestroy :public IComponent {};	//销毁标记

struct TagLifeTimeOut :public IComponent {};//生命周期到期标记

using SyncID = std::string;
struct TagSync : public IComponent
{
	enum class SyncType
	{
		FULL,          // 完全同步（位置、旋转、状态等）
		POSITION_ONLY, // 只同步位置
		STATE_ONLY,    // 只同步状态
		EVENT_ONLY     // 只同步事件
	};

	enum class SyncEntityType
	{
		TANK,
		BULLET,
		WALL,
		PROP
	};

	// 唯一标识符
	SyncID syncId;

	// 同步配置
	SyncType syncType = SyncType::FULL;
	SyncEntityType syncEntityType = SyncEntityType::TANK;
};

struct TagMainCamera :public IComponent {};

struct TagCamera :public IComponent {};
struct TagFreeCamera :public IComponent { float velocity = 0.25; };	//自由相机

struct TagCurrentControl :public IComponent {};

struct TagLight :public IComponent {};
struct TagLightShowLight :public IComponent {};

struct TagWeapon :public IComponent {};
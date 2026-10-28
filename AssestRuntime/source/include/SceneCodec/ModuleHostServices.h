#pragma once

#include "UI/MessageBus.h"

namespace NComponentCodec
{
	struct ModuleHostServices
	{
		// UI 事件总线
		// 为空表示宿主没有开 UI 服务，模块应该静默跳过注册
		UI::MessageBus* messageBus = nullptr;

		// serviceOwner 是引擎自己传回来的不透明句柄，调用request会用到
		void* serviceOwner = nullptr;
		void (*requestSceneChange)(void* serviceOwner, const char* sceneAsset) = nullptr;
		void (*requestQuit)(void* serviceOwner) = nullptr;
	};
}

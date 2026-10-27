#pragma once

// 引擎交给模块的"宿主服务句柄"。
//
// EngineModuleContext::hostServices 是个 void*，模块加载时由引擎填。这里放的是
// 引擎愿意暴露给模块的那几个能力入口。它和 EngineModuleAPI 的区别：
//   EngineModuleAPI  = 模块 -> 引擎（模块提供什么）
//   ModuleHostServices = 引擎 -> 模块（引擎提供什么）
//
// 不做版本/大小协商：模块和引擎必须用同一份头一起编译。加字段就直接加，
// 老模块重新编译，不保留"读到界外"的降级路径。

namespace UI
{
	class UICommandBus;
}

namespace NComponentCodec
{
	struct ModuleHostServices
	{
		// UI 事件总线：模块在这里 Register 自己关心的 command，引擎在场景线程上派发。
		// 为空表示宿主没有开 UI 服务，模块应该静默跳过注册。
		UI::UICommandBus* uiCommands = nullptr;

		// 切场景请求。模块不拥有线程，也不拥有 World 的生死，所以它只能"登记"，
		// 由引擎在宿主线程的帧边界上真正执行。用函数指针而不是虚接口，避免跨 DLL
		// 的 vtable / 编译器差异；owner 是引擎自己传回来的不透明句柄。
		void* sceneServiceOwner = nullptr;
		void (*requestSceneChange)(void* owner, const char* sceneAsset) = nullptr;
	};
}

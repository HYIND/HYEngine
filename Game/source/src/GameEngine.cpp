#include "stdafx.h"
#include "GameEngine.h"

#include "EngineRuntime.h"
#include "Helper/AudioTool.h"
#include "RenderEngine/Renderer.h"

#include <filesystem>

namespace
{
	// 模块仓库固定在 exe 同级的 Modules 下，和编辑器一致。
	std::string GetModulesDirectory()
	{
		wchar_t path[MAX_PATH] = {};
		const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
		if (length == 0 || length >= MAX_PATH)
			return {};
		return (std::filesystem::path(path).parent_path() / L"Modules").string();
	}
}

GameEngine& GameEngine::Instance()
{
	static GameEngine instance;
	return instance;
}

bool GameEngine::Initialize(std::string* error)
{
	if (m_runtime)
		return true;

	auto runtime = std::make_unique<EngineRuntime>();

	EngineRuntimeConfig config;
	config.host.host = EngineHostKind::Game;
	config.modules = { EngineRuntimeModule::Input, EngineRuntimeModule::Audio ,EngineRuntimeModule::Render };

	if (!runtime->Initialize(config))
	{
		if (error)
			*error = "EngineRuntime::Initialize 失败";
		return false;
	}

	// 模块加载失败不拦启动：大厅本身不需要玩法模块，等真正进游戏时再报错。
	std::string moduleError;
	if (!runtime->LoadModules(GetModulesDirectory(), &moduleError))
		m_moduleError = moduleError.empty() ? "未找到项目模块" : moduleError;

	m_runtime = std::move(runtime);
	return true;
}

void GameEngine::Shutdown()
{
	if (!m_runtime)
		return;

	m_runtime->Shutdown();
	m_runtime.reset();
}

EngineRuntime& GameEngine::Runtime()
{
	return *m_runtime;
}

std::shared_ptr<AudioDevice> GameEngine::Audio() const
{
	return m_runtime ? m_runtime->GetAudioDevice() : nullptr;
}

std::shared_ptr<EngineInputContext> GameEngine::Input() const
{
	return m_runtime ? m_runtime->GetInputContext() : nullptr;
}

bool GameEngine::CreateRenderer(uint32_t width, uint32_t height, std::string* error)
{
	if (!m_runtime)
	{
		if (error)
			*error = "引擎尚未初始化";
		return false;
	}

	EngineRenderContextDesc desc;
	desc.instanceExtensions = { "VK_KHR_surface", "VK_KHR_win32_surface" };
	desc.width = width;
	desc.height = height;
	if (!m_runtime->CreateRenderContext(desc, error))
		return false;

	auto* renderer = m_runtime->GetRenderer();
	if (!renderer)
	{
		if (error)
			*error = "渲染服务不可用";
		return false;
	}

	auto* renderer3D = renderer->GetRenderer3D();
	if (!renderer3D)
	{
		if (error)
			*error = "Renderer3D 不可用";
		return false;
	}

	renderer3D->StartBackend();
	if (auto* renderer = m_runtime->GetRenderer())
		renderer->Start();
	else
		renderer3D->Start();
	return true;
}

void GameEngine::SetTargetFps(uint32_t fps)
{
	if (!m_runtime)
		return;

	auto* renderer = m_runtime->GetRenderer();
	if (!renderer)
		return;

	if (auto* renderer3D = renderer->GetRenderer3D())
		renderer3D->SetTargetFps(fps);
}

uint32_t GameEngine::GetTargetFps() const
{
	if (!m_runtime)
		return 0;

	auto* renderer = m_runtime->GetRenderer();
	if (!renderer)
		return 0;

	if (auto* renderer3D = renderer->GetRenderer3D())
		return renderer3D->GetTargetFps();

	return 0;
}

void GameEngine::StartSceneThread(float logicDeltaMilliseconds, float fixedDeltaMilliseconds)
{
	if (m_runtime)
		m_runtime->StartSceneThread(logicDeltaMilliseconds, fixedDeltaMilliseconds);
}

void GameEngine::StopSceneThread()
{
	if (m_runtime)
		m_runtime->StopSceneThread();
}

void GameEngine::CloseScene()
{
	if (m_runtime)
		m_runtime->CloseScene();
}

#pragma once

#include <memory>
#include <cstdint>
#include <string>

#include "EngineRuntime.h"

class EngineInputContext;
class AudioDevice;

// Game 进程里唯一的一份引擎运行时。
class GameEngine
{
public:
    static GameEngine& Instance();

    // 窗口建好之后调用一次。失败时 error 里带原因。
    bool Initialize(std::string* error = nullptr);
    void Shutdown();
    bool IsInitialized() const { return m_runtime != nullptr; }

    EngineRuntime& Runtime();

    std::shared_ptr<AudioDevice> Audio() const;
    std::shared_ptr<EngineInputContext> Input() const;

	bool CreateRenderer(uint32_t width, uint32_t height, std::string* error);

    void SetTargetFps(uint32_t fps);
    uint32_t GetTargetFps() const;

    void StartSceneThread(float logicDeltaMilliseconds, float fixedDeltaMilliseconds);
    void StopSceneThread();
    void CloseScene();

private:
    GameEngine() = default;
    ~GameEngine() = default;
    GameEngine(const GameEngine&) = delete;
    GameEngine& operator=(const GameEngine&) = delete;

    std::unique_ptr<EngineRuntime> m_runtime;
    std::string m_moduleError;
};

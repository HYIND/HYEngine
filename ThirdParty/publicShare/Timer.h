#pragma once

#include <functional>
#include <memory>
#include <string>
#include <chrono>

#include "PublicShareExportMacro.h"

// 获取运行时间间隔
class PUBLICSHARE_API TimerElapsed {
private:
    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point end_time;
    bool running = false;

public:
    void start() {
        start_time = std::chrono::high_resolution_clock::now();
        running = true;
    }

    void stop() {
        if (running) {
            end_time = std::chrono::high_resolution_clock::now();
            running = false;
        }
    }

    // 获取微秒
    long long elapsed_microseconds() const {
        auto end = running ? std::chrono::high_resolution_clock::now() : end_time;
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start_time).count();
    }

    // 获取毫秒
    long long elapsed_milliseconds() const {
        auto end = running ? std::chrono::high_resolution_clock::now() : end_time;
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start_time).count();
    }

    // 获取秒（浮点数）
    double elapsed_seconds() const {
        auto end = running ? std::chrono::high_resolution_clock::now() : end_time;
        return std::chrono::duration<double>(end - start_time).count();
    }
};

struct TimerTaskHandle;

class PUBLICSHARE_API TimerTask
{
public:
    using Callback = std::function<void()>;

    static std::shared_ptr<TimerTask> CreateOnce(
        const std::string &name,
        uint64_t delay_ms,
        Callback callback);

    static std::shared_ptr<TimerTask> CreateRepeat(
        const std::string &name,
        uint64_t interval_ms,
        Callback callback,
        uint64_t delay_ms = 0);

    bool Run();
    bool Stop();

    bool Wake();

    ~TimerTask();
    void Clean(); // 关闭定时器并清理资源

public:
    const std::string &name();
    uint64_t interval();
    bool is_repeat();
    bool is_valid();
    int timer_fd();
    Callback callback();
    void mark_invalid();

private:
    TimerTask(const std::string &name,
              uint64_t interval_ms,
              bool repeat,
              Callback callback,
              uint64_t delay_ms = 0);

    TimerTask(const TimerTask &) = delete;
    TimerTask &operator=(const TimerTask &) = delete;

private:
    std::shared_ptr<TimerTaskHandle> _handle;
};

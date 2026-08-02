#pragma once

#ifdef __linux__
#include <pthread.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <condition_variable>
#include <chrono>
#include <concepts>

#include "PublicShareExportMacro.h"

class PUBLICSHARE_API CriticalSectionLock
{
public:
	CriticalSectionLock();
	~CriticalSectionLock();
	bool TryEnter();
	void Enter();
	void Leave();

	// 适配std::lock_guard、condition_variable
public:
	bool try_lock();
	void lock();
	void unlock();

private:
#ifdef __linux__
	pthread_mutex_t _mutex;
	pthread_mutexattr_t _attr;
#elif defined(_WIN32)
	CRITICAL_SECTION _cs;
#endif
};

template<typename T>
concept TryLockable = requires(T & lock) {
	lock.lock();
	lock.unlock();
	{ lock.try_lock() } -> std::convertible_to<bool>;
};
template<typename T>
concept Lockable = requires(T & lock) {
	lock.lock();
	lock.unlock();
};

template<TryLockable T>
class LockGuard
{
public:
	LockGuard(T& mutex, bool istrylock = false)
		: _mutex(mutex), _isownlock(false)
	{
		if (istrylock)
			_isownlock = _mutex.try_lock();
		else
			lock();
	}

	~LockGuard() {
		unlock();
	}

	bool isownlock() const { return _isownlock; }

	void lock()
	{
		if (!_isownlock)
		{
			_mutex.lock();
			_isownlock = true;
		}
	}

	void unlock() {
		if (_isownlock) {
			_mutex.unlock();
			_isownlock = false;
		}
	}

	LockGuard(const LockGuard&) = delete;
	LockGuard& operator=(const LockGuard&) = delete;
	LockGuard(LockGuard&&) = delete;
	LockGuard& operator=(LockGuard&&) = delete;

private:
	T& _mutex;
	bool _isownlock;
};

template<TryLockable T>
LockGuard(T&, bool) -> LockGuard<T>;


class PUBLICSHARE_API ConditionVariable
{
public:
	ConditionVariable() = default;
	~ConditionVariable() = default;

	ConditionVariable(const ConditionVariable&) = delete;
	ConditionVariable& operator=(const ConditionVariable&) = delete;
	ConditionVariable(ConditionVariable&&) = delete;
	ConditionVariable& operator=(ConditionVariable&&) = delete;

	template<Lockable T>
	void Wait(T& lock)
	{
		_cv.wait(lock);
	}
	template<Lockable T>
	bool WaitFor(T& lock, const std::chrono::milliseconds ms)
	{
		return _cv.wait_for(lock, ms) == std::cv_status::timeout;
	}
	template <Lockable T, class BoolFunc>
	bool WaitFor(T& lock, const std::chrono::milliseconds ms, BoolFunc func)
	{
		return _cv.wait_for(lock, ms, func);
	}

	void NotifyAll() { _cv.notify_all(); }
	void NotifyOne() { _cv.notify_one(); }

private:
	std::condition_variable_any _cv;
};

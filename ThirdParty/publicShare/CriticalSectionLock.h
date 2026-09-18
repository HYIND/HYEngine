#pragma once

#ifdef __linux__
#include <pthread.h>
#include <shared_mutex>
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

class PUBLICSHARE_API SharedLock
{
public:
	SharedLock();
	~SharedLock();
	bool try_lock() noexcept;
	void lock() noexcept;
	void unlock() noexcept;
	bool try_lock_shared() noexcept;
	void lock_shared() noexcept;
	void unlock_shared() noexcept;

private:
#ifdef __linux__
	std::shared_mutex _mutex;
#elif defined(_WIN32)
	SRWLOCK _srwlock;
#endif
};

template<typename T>
concept TryLockable = requires(T & mutex) {
	mutex.lock();
	mutex.unlock();
	{ mutex.try_lock() } -> std::convertible_to<bool>;
};

template<typename T>
concept Lockable = requires(T & mutex) {
	mutex.lock();
	mutex.unlock();
};

template<typename T>
concept TrySharedLockable = requires(T & mutex) {
	mutex.lock_shared();
	mutex.unlock_shared();
	{ mutex.try_lock_shared() } -> std::convertible_to<bool>;
};

template<typename T>
concept SharedLockable = requires(T & mutex) {
	mutex.lock_shared();
	mutex.unlock_shared();
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

template<TrySharedLockable T>
class SharedLockGuard
{
public:
	SharedLockGuard(T& mutex, bool istrylock = false)
		: _mutex(mutex), _isownlock(false)
	{
		if (istrylock)
			_isownlock = _mutex.try_lock_shared();
		else
			lock();
	}

	~SharedLockGuard() {
		unlock();
	}

	bool isownlock() const { return _isownlock; }

	void lock()
	{
		if (!_isownlock)
		{
			_mutex.lock_shared();
			_isownlock = true;
		}
	}

	void unlock() {
		if (_isownlock) {
			_mutex.unlock_shared();
			_isownlock = false;
		}
	}

	SharedLockGuard(const SharedLockGuard&) = delete;
	SharedLockGuard& operator=(const SharedLockGuard&) = delete;
	SharedLockGuard(SharedLockGuard&&) = delete;
	SharedLockGuard& operator=(SharedLockGuard&&) = delete;

private:
	T& _mutex;
	bool _isownlock;
};

template<TrySharedLockable T>
SharedLockGuard(T&, bool) -> SharedLockGuard<T>;

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
	template <Lockable T, class BoolFunc>
	void Wait(T& lock, BoolFunc func)
	{
		_cv.wait(lock, func);
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

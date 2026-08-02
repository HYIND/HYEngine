#pragma once

#include <atomic>
#include <thread>

#include "PublicShareExportMacro.h"


// 自旋锁
class PUBLICSHARE_API SpinLock
{
public:
	SpinLock();
	SpinLock(const SpinLock&) = delete;
	SpinLock& operator=(const SpinLock) = delete;
	bool try_lock();
	void lock();
	void unlock();

private:
	std::atomic_flag _flag;
};

// 自旋锁
class PUBLICSHARE_API RecursiveSpinLock
{
public:
	RecursiveSpinLock();
	RecursiveSpinLock(const RecursiveSpinLock&) = delete;
	RecursiveSpinLock& operator=(const RecursiveSpinLock) = delete;
	bool try_lock();
	void lock();
	void unlock();

	bool is_locked()const;
	int recursion_count()const;

private:
	std::atomic_flag _flag;
	std::thread::id _owner;    // 持有锁的线程ID
	int _recursion_count = 0;  // 递归计数
};
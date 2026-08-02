#pragma once

#include <thread>
#include "SafeStl.h"
#include "CriticalSectionLock.h"
#include <vector>
#include <condition_variable>
#include <functional>
#include <future>

#include "PublicShareExportMacro.h"

// 线程池
class PUBLICSHARE_API FlexThreadPool
{
public:
	template <typename T>
	class SubmitHandle
	{
	private:
		std::shared_ptr<std::packaged_task<T()>> task_ptr;
		std::weak_ptr<int> taskliveflag;

	public:
		SubmitHandle(std::shared_ptr<std::packaged_task<T()>> task_ptr, std::weak_ptr<int> taskliveflag)
			: task_ptr(std::move(task_ptr)), taskliveflag(taskliveflag) {
		}

		SubmitHandle(const SubmitHandle&) = delete;
		SubmitHandle& operator=(const SubmitHandle&) = delete;

		SubmitHandle(SubmitHandle&& other) noexcept
			: task_ptr(std::move(other.task_ptr)), taskliveflag(std::move(other.taskliveflag)) {
		}

		SubmitHandle& operator=(SubmitHandle&& other) noexcept
		{
			if (this != &other)
			{
				task_ptr = std::move(other.task_ptr);
				taskliveflag = std::move(other.taskliveflag);
			}
			return *this;
		}

		std::future<T> get_future() const
		{
			if (!task_ptr)
				throw std::future_error(std::future_errc::no_state);

			return task_ptr->get_future();
		}

		T get()
		{
			if (!task_ptr)
				throw std::future_error(std::future_errc::no_state);

			auto fut = task_ptr->get_future();
			if (taskliveflag.expired())
			{
				if (fut.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
				{
					try
					{
						return fut.get();
					}
					catch (...)
					{
						throw;
					}
				}
				else
					throw std::runtime_error("ThreadPool is force stopped!");
			}

			while (true)
			{
				auto status = fut.wait_for(std::chrono::milliseconds(10));
				if (status == std::future_status::ready)
				{
					try
					{
						return fut.get();
					}
					catch (...)
					{
						throw;
					}
				}
				if (status == std::future_status::timeout)
				{
					if (taskliveflag.expired())
						throw std::runtime_error("ThreadPool is force stopped!");
					continue;
				}
				throw std::runtime_error("Task future_status error!");
			}
		}

		template <typename Rep, typename Period>
		T get_with_timeout(const std::chrono::duration<Rep, Period>& timeout_duration)
		{
			if (!task_ptr)
				throw std::future_error(std::future_errc::no_state);

			auto fut = task_ptr->get_future();
			if (taskliveflag.expired())
			{
				auto status = fut.wait_for(std::chrono::seconds(0));
				if (status == std::future_status::ready)
				{
					try
					{
						return fut.get();
					}
					catch (...)
					{
						throw;
					}
				}
				if (status == std::future_status::timeout)
					throw std::runtime_error("ThreadPool is force stopped!");
			}

			auto status = fut.wait_for(timeout_duration);
			if (status == std::future_status::ready)
			{
				try
				{
					return fut.get();
				}
				catch (...)
				{
					throw;
				}
			}
			if (status == std::future_status::timeout)
			{
				if (taskliveflag.expired())
					throw std::runtime_error("ThreadPool is force stopped!");
				throw std::runtime_error("Task timeout");
			}
			throw std::runtime_error("Task future_status error!");
		}

		bool valid() const
		{
			return task_ptr != nullptr;
		}

		~SubmitHandle() = default;
	};

private:
	struct ThreadData
	{
		struct ThreadTask
		{
			std::function<void()> func;
			std::shared_ptr<int> taskliveflag;
			ThreadTask(std::function<void()> func);
		};

		std::shared_ptr<std::thread> thread;

		SafeQueue<std::unique_ptr<ThreadTask>> queue;
		CriticalSectionLock queue_mutex;
		ConditionVariable queue_cv;

		bool _is_idle = true;
		int64_t last_active_time = 0;

		std::atomic<bool> _stop{ true };
		int id;
	};
	class ThreadWorker // 线程工作类
	{
		using ThreadTask = FlexThreadPool::ThreadData::ThreadTask;

	private:
		FlexThreadPool* m_pool;             // 所属线程池
		std::shared_ptr<ThreadData> m_data; // 线程信息
		int last_steal_threadid;

	public:
		ThreadWorker(FlexThreadPool* pool, std::shared_ptr<ThreadData> data);
		void operator()();

	private:
		bool stealTask(std::unique_ptr<ThreadTask>& task); // 窃取任务
	};

public:
	FlexThreadPool(uint32_t threads_maxnum = 0, uint32_t worker_expire_second = 30, bool worksteal = true);
	~FlexThreadPool();

	FlexThreadPool(const FlexThreadPool&) = delete;
	FlexThreadPool(FlexThreadPool&&) = delete;
	FlexThreadPool& operator=(const FlexThreadPool&) = delete;
	FlexThreadPool& operator=(FlexThreadPool&&) = delete;

	void start();
	void stop();
	void stopnow();
	bool running();
	uint32_t workermaxsize();

private:
	void StartThread(std::shared_ptr<ThreadData> data);
	std::weak_ptr<int> CommitToAvailablieThread(std::function<void()> func);
	void MonitorThreadFunc();

public:
	template <typename F, typename... Args>
	auto submit(F&& f, Args &&...args)
		-> std::shared_ptr<FlexThreadPool::SubmitHandle<std::invoke_result_t<F, Args...>>>
	{
		using ReturnType = std::invoke_result_t<F, Args...>;
		auto task_ptr = std::make_shared<std::packaged_task<ReturnType()>>(
			[f = std::forward<F>(f),
			... args = std::forward<Args>(args)]() mutable
			{
				return std::invoke(std::move(f), std::move(args)...);
			});

		std::function<void()> warpper_func =
			[task_ptr]()
			{
				(*task_ptr)();
			};

		auto taskliveflag = CommitToAvailablieThread(warpper_func);

		return std::make_shared<FlexThreadPool::SubmitHandle<ReturnType>>(task_ptr, taskliveflag);
	}

private:
	std::atomic<bool> _stop;
	uint32_t _threadscount;
	std::vector<std::shared_ptr<ThreadData>> _threads;

	std::shared_ptr<std::thread> _monitorthread;
	CriticalSectionLock _monitor_mutex;
	ConditionVariable _monitor_cv;

	uint32_t _workerExpireTime;

	bool _worksteal;
};
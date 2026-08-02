#pragma once

#include <thread>
#include "SafeStl.h"
#include <vector>
#include <condition_variable>
#include <functional>
#include <future>

#include "PublicShareExportMacro.h"

// 线程池
class PUBLICSHARE_API ThreadPool
{
public:
	template <typename T>
	class SubmitHandle
	{
	private:
		std::shared_ptr<std::packaged_task<T()>> task_ptr;
		uint32_t thread_id;
		std::weak_ptr<int> taskliveflag;

	public:
		SubmitHandle(std::shared_ptr<std::packaged_task<T()>> task_ptr, uint32_t thread_id, std::weak_ptr<int> taskliveflag)
			: task_ptr(std::move(task_ptr)), thread_id(thread_id), taskliveflag(taskliveflag) {
		}

		SubmitHandle(const SubmitHandle&) = delete;
		SubmitHandle& operator=(const SubmitHandle&) = delete;

		SubmitHandle(SubmitHandle&& other) noexcept
			: task_ptr(std::move(other.task_ptr)), thread_id(other.thread_id), taskliveflag(std::move(other.taskliveflag))
		{
			other.thread_id = 0;
		}

		SubmitHandle& operator=(SubmitHandle&& other) noexcept
		{
			if (this != &other)
			{
				task_ptr = std::move(other.task_ptr);
				thread_id = other.thread_id;
				taskliveflag = std::move(other.taskliveflag);
				other.thread_id = 0;
			}
			return *this;
		}

		std::future<T> get_future() const
		{
			if (!task_ptr)
			{
				throw std::future_error(std::future_errc::no_state);
			}
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

		uint32_t get_thread_id() const
		{
			return thread_id;
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

		std::thread thread;

		SafeQueue<std::unique_ptr<ThreadTask>> queue;
		CriticalSectionLock queue_mutex;
		ConditionVariable queue_cv;

		bool _is_idle = true;

		std::atomic<bool> _stop{ true };
		int id;
	};
	class ThreadWorker // 内置线程工作类
	{
		using ThreadTask = ThreadPool::ThreadData::ThreadTask;

	private:
		ThreadPool* m_pool;                 // 所属线程池
		std::shared_ptr<ThreadData> m_data; // 线程信息
	public:
		ThreadWorker(ThreadPool* pool, std::shared_ptr<ThreadData> data);
		void operator()();
		void yield_until(std::function<bool()>&& predicate);
	};

	class YieldWorker
	{
		using ThreadTask = ThreadPool::ThreadData::ThreadTask;

	private:
		ThreadPool* m_pool;                 // 所属线程池
		std::shared_ptr<ThreadData> m_data; // 线程信息
		std::function<bool()> m_predicate;
	public:
		YieldWorker(ThreadPool* pool, std::shared_ptr<ThreadData> data, std::function<bool()> predicate);
		void operator()();
	};

public:
	ThreadPool(uint32_t threads_num = 0);
	~ThreadPool();

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool(ThreadPool&&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
	ThreadPool& operator=(ThreadPool&&) = delete;

	void start();
	void stop();
	void stopnow();
	bool running();
	uint32_t workersize();

	static bool can_yield();
	static void yield_until(std::function<bool()> predicate);

private:
	uint32_t GetAvailablieThread();
	std::weak_ptr<int> CommitTask(uint32_t thread_id, std::function<void()> func);

public:
	template <typename F, typename... Args>
	auto submit_to(uint32_t thread_id, F&& f, Args &&...args) // 指定线程id
		-> std::shared_ptr<ThreadPool::SubmitHandle<std::invoke_result_t<F, Args...>>>
	{
		if (thread_id >= _threads.size())
			throw std::out_of_range("Invalid thread id");

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

		auto taskliveflag = CommitTask(thread_id, warpper_func);

		return std::make_shared<ThreadPool::SubmitHandle<ReturnType>>(task_ptr, thread_id, taskliveflag); // 返回Handle
	}

	template <typename F, typename... Args>
	auto submit(F&& f, Args &&...args) // 随机分配线程id
		-> std::shared_ptr<ThreadPool::SubmitHandle<std::invoke_result_t<F, Args...>>>
	{
		uint32_t thread_id = GetAvailablieThread();
		return submit_to(thread_id, std::forward<F>(f), std::forward<Args>(args)...);
	}

private:
	std::atomic<bool> _stop;
	uint32_t _threadscount;
	std::vector<std::shared_ptr<ThreadData>> _threads;
};
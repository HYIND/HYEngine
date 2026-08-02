#pragma once

#ifdef __linux__
#include <netinet/in.h>
#else
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Mswsock.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "mswsock.lib")
#endif

#include <memory>
#include <coroutine>
#include <optional>
#include <type_traits>
#include <utility>
#include "CriticalSectionLock.h"
#include <atomic>
#include <stdexcept>
#include <thread>
#include "Timer.h"

#include "PublicShareExportMacro.h"

#ifndef BASESOCKET_DEFINE
#define BASESOCKET_DEFINE
#ifdef __linux__
using BaseSocket = int;
#elif _WIN32
using BaseSocket = SOCKET;
#endif
#endif

bool PUBLICSHARE_API Can_Yield();
void PUBLICSHARE_API Yield_Until(std::function<bool()> predicate);

class CoroutineContextImpl;
using CoroHandle = std::coroutine_handle<>;
class PUBLICSHARE_API CoroutineContext
{

public:
	// ==================== 协程线程标记 ====================
	static bool isCoroutineThread();	// 检查当前线程是否是协程线程

	// ==================== 当前协程管理 ====================
	static void setCurrent(CoroHandle coro);	// 设置当前正在执行的协程（在resume前调用）
	static CoroHandle getCurrent();	// 获取当前正在执行的协程
	static void clearCurrent();	// 清除当前协程

	// ==================== 协程关系管理 ====================
	static void setParent(CoroHandle child, CoroHandle parent);			// 设置父子关系（在co_await时调用）
	static void setParent(CoroHandle child, std::thread::id parent);// 设置父子关系（在sync_wait时调用）
	static CoroHandle getCurrentRoot();			// 获取当前协程的根协程
	static CoroHandle getRoot(CoroHandle coro);		// 获取指定协程的根协程
	static void clearParent(CoroHandle child);	// 协程结束时清除关系

	// ==================== 关系判断 ====================
	static bool isAncestor(CoroHandle descendant, CoroHandle ancestor);				// 检查一个协程是否是另一个协程的祖先
	static bool isAncestor(CoroHandle descendant, std::thread::id threadid);	// 检查一个普通线程是否在sync_wait协程

	template<typename T>
	static bool setData(const std::string& key, T&& value) {
		return setDataImpl(key, std::make_shared<T>(std::forward<T>(value)));
	}
	template<typename T>
	static T* getData(const std::string& key, bool skipcurrent = false) {
		return static_cast<T*>(getDataImpl(key, skipcurrent));
	}

private:
	static bool setDataImpl(const std::string& key, std::shared_ptr<void> ptr);
	static void* getDataImpl(const std::string& key, bool skipcurrent = false);
	static CoroutineContextImpl& getImpl();
};

PUBLICSHARE_API std::coroutine_handle<> DeleteLater(std::shared_ptr<std::coroutine_handle<>> p);

struct PUBLICSHARE_API yieldAwaiter
{
	bool await_ready();
	void await_suspend(std::coroutine_handle<> coro);
	void await_resume();
};

// 实现co_await yield()
// 让出协程
class PUBLICSHARE_API yield
{
public:
	yieldAwaiter operator co_await();
};

struct PUBLICSHARE_API TaskHandle
{
	std::coroutine_handle<> coroutine;

	TaskHandle(std::coroutine_handle<> coroutine);
	~TaskHandle();
};

template <typename T>
class Task;
template <typename T>
struct TaskPromiseBase
{

	std::exception_ptr exception_;
	std::coroutine_handle<> continuation_;

	std::shared_ptr<CriticalSectionLock> _continuationlock = std::make_shared<CriticalSectionLock>();
	std::shared_ptr<ConditionVariable> _syncwaitcv = std::make_shared<ConditionVariable>();

	auto initial_suspend() noexcept
	{
		return yieldAwaiter{};
	}

	auto final_suspend() noexcept
	{

		struct FinalAwaiter
		{
			TaskPromiseBase* promise;

			bool await_ready() noexcept { return false; }
			std::coroutine_handle<> await_suspend(std::coroutine_handle<> completed_coro) noexcept
			{
				LockGuard lock(*(promise->_continuationlock));
				auto continuation = promise->continuation_;

				promise->_syncwaitcv->NotifyAll();

				if (continuation)
				{
					CoroutineContext::setCurrent(continuation);
					return continuation;
				}
				else
				{
					auto p = promise->on_post_final_suspend();
					auto delete_coro = DeleteLater(std::move(p));

					CoroutineContext::setCurrent(delete_coro);
					return delete_coro;
				}
			}

			void await_resume() noexcept {}
		};

		return FinalAwaiter{ this };
	}

	void unhandled_exception() noexcept
	{
		exception_ = std::current_exception();
	}

	void set_continuation(std::coroutine_handle<> continuation) noexcept
	{
		continuation_ = continuation;
	}

	void co_await_Delete()
	{
		on_post_final_suspend().reset();
	}

	virtual std::shared_ptr<std::coroutine_handle<>> on_post_final_suspend() = 0;
};

// 非 void 类型的 Promise
template <typename T>
struct TaskPromise : TaskPromiseBase<T>
{
	using CoroHandleType = std::coroutine_handle<TaskPromise<T>>;
	std::shared_ptr<CoroHandleType> _shared_handle;

	std::optional<T> value_;

	Task<T> get_return_object() noexcept;

	void return_value(T&& value)
	{
		value_.emplace(std::move(value));
	}

	void return_value(const T& value)
	{
		value_.emplace(value);
	}

	T& result()
	{
		if (this->exception_)
		{
			std::rethrow_exception(this->exception_);
		}
		return value_.value();
	}

	virtual std::shared_ptr<std::coroutine_handle<>> on_post_final_suspend() override
	{
		// 把带类型的_shared_handle转换成原始类型的shared_ptr
		// _shared_handle指向的内容保留在控制块的deleter中
		std::shared_ptr<std::coroutine_handle<>> contorl_block_sharedptr(
			_shared_handle,
			nullptr);
		_shared_handle.reset();
		return contorl_block_sharedptr;
	}

	~TaskPromise() = default;
};

// void 特化的 Promise
template <>
struct TaskPromise<void> : TaskPromiseBase<void>
{
	using CoroHandleType = std::coroutine_handle<TaskPromise<void>>;
	std::shared_ptr<CoroHandleType> _shared_handle;

	Task<void> get_return_object() noexcept;

	void return_void() noexcept {}

	void result()
	{
		if (exception_)
		{
			std::rethrow_exception(exception_);
		}
	}

	virtual std::shared_ptr<std::coroutine_handle<>> on_post_final_suspend() override
	{
		// 把带类型的_shared_handle转换成原始类型的shared_ptr
		// _shared_handle指向的内容保留在控制块的deleter中
		std::shared_ptr<std::coroutine_handle<>> contorl_block_sharedptr(
			_shared_handle,
			nullptr);
		_shared_handle.reset();
		return contorl_block_sharedptr;
	}

	~TaskPromise() = default;
};

template <typename T>
struct TaskAwaiter
{
	std::coroutine_handle<TaskPromise<T>> coroutine_;
	std::shared_ptr<CriticalSectionLock> _continuationlock;
	TaskAwaiter(std::coroutine_handle<TaskPromise<T>> coroutine, std::shared_ptr<CriticalSectionLock> lock) noexcept
		: coroutine_(coroutine), _continuationlock(lock)
	{
	}

	bool await_ready() noexcept
	{
		return coroutine_ && coroutine_.done();
	}

	bool await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept
	{
		if (!coroutine_ || coroutine_.done())
		{
			return false; // 无效协程，不挂起
		}

		LockGuard lock(*_continuationlock);
		if (coroutine_.done())
			return false;

		auto& promise = coroutine_.promise();
		promise.set_continuation(awaiting_coroutine);
		CoroutineContext::setParent(coroutine_, awaiting_coroutine);
		return true;
	}

	// 非 void 版本的 await_resume
	template <typename U = T>
	std::enable_if_t<!std::is_void_v<U>, U> await_resume()
	{
		if (!coroutine_)
			throw std::runtime_error("Invalid coroutine");
		if (!coroutine_.done())
			throw std::runtime_error("Coroutine not completed");

		T value;
		try
		{
			value = std::move(coroutine_.promise().result());
		}
		catch (...)
		{
			CoroutineContext::clearParent(coroutine_);
			coroutine_.promise().co_await_Delete();
			throw;
		}
		CoroutineContext::clearParent(coroutine_);
		coroutine_.promise().co_await_Delete();
		return value;
	}

	// void 特化的 await_resume
	template <typename U = T>
	std::enable_if_t<std::is_void_v<U>, void> await_resume()
	{
		if (!coroutine_)
			throw std::runtime_error("Invalid coroutine");
		if (!coroutine_.done())
			throw std::runtime_error("Coroutine not completed");

		try
		{
			coroutine_.promise().result();
		}
		catch (...)
		{
			CoroutineContext::clearParent(coroutine_);
			coroutine_.promise().co_await_Delete();
			throw;
		}
		CoroutineContext::clearParent(coroutine_);
		coroutine_.promise().co_await_Delete();
	}
};

template <typename T>
class Task
{
public:
	using promise_type = TaskPromise<T>;
	using value_type = T;

	Task() {};
	explicit Task(std::shared_ptr<std::coroutine_handle<promise_type>> shared_coroutine, std::shared_ptr<CriticalSectionLock> lock, std::shared_ptr<ConditionVariable> cv) noexcept
		: _shared_coroutine(shared_coroutine), _continuationlock(lock), _syncwaitcv(cv)
	{
	}

	Task(Task&& other) noexcept
		: _shared_coroutine(std::move(other._shared_coroutine)), _continuationlock(other._continuationlock), _syncwaitcv(other._syncwaitcv)
	{
		other._continuationlock.reset();
		other._syncwaitcv.reset();
	}

	Task& operator=(Task&& other) noexcept
	{
		if (this != &other)
		{
			_shared_coroutine = std::move(other._shared_coroutine);
			_continuationlock = other._continuationlock;
			_syncwaitcv = other._syncwaitcv;

			other._continuationlock.reset();
			other._syncwaitcv.reset();
		}
		return *this;
	}

	Task(const Task&) = delete;
	Task& operator=(const Task&) = delete;

	~Task()
	{
	}

	TaskAwaiter<T> operator co_await() const noexcept
	{
		return TaskAwaiter<T>{*_shared_coroutine, _continuationlock};
	}

	template <typename U = T>
	std::enable_if_t<!std::is_void_v<U>, U> sync_wait()
	{
		if (!_shared_coroutine || !_continuationlock || !_syncwaitcv)
			throw std::runtime_error("Invalid Coroutine");

		if (*_shared_coroutine)
		{
			if (CoroutineContext::isCoroutineThread() && Can_Yield())
			{
				auto current = CoroutineContext::getCurrent();
				CoroutineContext::setParent(*_shared_coroutine, current);

				if ((*_shared_coroutine).done())
					return (*_shared_coroutine).promise().result();

				while (!(*_shared_coroutine).done())
				{
					Yield_Until([coro_ptr = this->_shared_coroutine]()->bool {return (*coro_ptr).done(); });
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				}
				CoroutineContext::setCurrent(current);
				CoroutineContext::clearParent(*_shared_coroutine);
				return (*_shared_coroutine).promise().result();
			}

			LockGuard guard(*_continuationlock);
			if ((*_shared_coroutine).done())
				return (*_shared_coroutine).promise().result();

			if (CoroutineContext::isCoroutineThread())
				CoroutineContext::setParent(*_shared_coroutine, CoroutineContext::getCurrent());
			else
				CoroutineContext::setParent(*_shared_coroutine, std::this_thread::get_id());
			_syncwaitcv->Wait(guard);
			CoroutineContext::clearParent(*_shared_coroutine);
			return (*_shared_coroutine).promise().result();
		}
		throw std::runtime_error("Invalid task");
	}

	template <typename U = T>
	std::enable_if_t<std::is_void_v<U>, void> sync_wait()
	{
		if (!_shared_coroutine || !_continuationlock || !_syncwaitcv)
			throw std::runtime_error("Invalid Coroutine");

		if (*_shared_coroutine)
		{
			if (CoroutineContext::isCoroutineThread() && Can_Yield())
			{
				auto current = CoroutineContext::getCurrent();
				CoroutineContext::setParent(*_shared_coroutine, current);

				if ((*_shared_coroutine).done())
					return (*_shared_coroutine).promise().result();

				while (!(*_shared_coroutine).done())
				{
					Yield_Until([coro_ptr = this->_shared_coroutine]()->bool {return (*coro_ptr).done(); });
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				}
				CoroutineContext::setCurrent(current);
				CoroutineContext::clearParent(*_shared_coroutine);
				return (*_shared_coroutine).promise().result();
			}

			LockGuard guard(*_continuationlock);
			if ((*_shared_coroutine).done())
			{
				(*_shared_coroutine).promise().result();
				return;
			}

			if (CoroutineContext::isCoroutineThread())
				CoroutineContext::setParent(*_shared_coroutine, CoroutineContext::getCurrent());
			else
				CoroutineContext::setParent(*_shared_coroutine, std::this_thread::get_id());
			_syncwaitcv->Wait(guard);
			CoroutineContext::clearParent(*_shared_coroutine);
			(*_shared_coroutine).promise().result();
			return;
		}
		throw std::runtime_error("Invalid task");
	}

	bool is_done() const
	{
		if (!_shared_coroutine)
			throw std::runtime_error("Invalid Coroutine");
		return !(*_shared_coroutine) || (*_shared_coroutine).done();
	}

	explicit operator bool() const noexcept
	{
		return bool(_shared_coroutine) && bool(*_shared_coroutine);
	}

private:
	std::shared_ptr<std::coroutine_handle<promise_type>> _shared_coroutine;
	std::shared_ptr<CriticalSectionLock> _continuationlock;
	std::shared_ptr<ConditionVariable> _syncwaitcv;

	template <typename U>
	friend struct TaskPromise;
};

template <typename T>
inline Task<T> TaskPromise<T>::get_return_object() noexcept
{
	auto raw_handle = CoroHandleType::from_promise(*this);
	_shared_handle = std::shared_ptr<CoroHandleType>(new CoroHandleType(raw_handle),
		[](CoroHandleType* handle)
		{
			if (handle)
			{
				if (*handle)
				{
					handle->destroy();
				}
				delete handle;
			}
		});

	return Task<T>(
		_shared_handle,
		TaskPromiseBase<T>::_continuationlock,
		TaskPromiseBase<T>::_syncwaitcv);
}

inline Task<void> TaskPromise<void>::get_return_object() noexcept
{
	auto raw_handle = CoroHandleType::from_promise(*this);
	_shared_handle = std::shared_ptr<CoroHandleType>(new CoroHandleType(raw_handle),
		[](CoroHandleType* handle)
		{
			if (handle)
			{
				if (*handle)
				{
					handle->destroy();
				}
				delete handle;
			}
		});

	return Task<void>(
		_shared_handle,
		TaskPromiseBase<void>::_continuationlock,
		TaskPromiseBase<void>::_syncwaitcv);
}

namespace CoroTask
{
	template <typename U>
	struct is_task : std::false_type
	{
	};
	template <typename T>
	struct is_task<Task<T>> : std::true_type
	{
	};
	template <typename T>
	static constexpr bool is_task_v = is_task<T>::value;

	template <typename T>
	struct flatten_task
	{
		using type = T;
	};

	template <typename T>
	struct flatten_task<Task<T>>
	{
		using type = T; // 展开一层
	};

	template <typename T>
	using flatten_task_t = typename flatten_task<T>::type;

	template <typename Callable>
	static auto Run(Callable callable)
		-> Task<flatten_task_t<decltype(callable())>>
	{
		using RawReturnType = decltype(callable());

		if constexpr (is_task_v<RawReturnType>)
		{
			co_return co_await callable();
		}
		else if constexpr (std::is_void_v<RawReturnType>)
		{
			callable();
			co_return;
		}
		else
		{
			co_return callable();
		}
	}
}

// 协程定时器包装器
class PUBLICSHARE_API CoTimer
{
public:
	enum class PUBLICSHARE_API WakeType
	{
		Error = -1,
		RUNNING = 0,
		TIMEOUT,
		MANUAL_WAKE
	};

	struct PUBLICSHARE_API Handle
	{
		Handle();
		~Handle();

		std::shared_ptr<TimerTask> task;
		bool active;

		std::coroutine_handle<> coroutine;
		std::atomic<bool> corodone;
		CriticalSectionLock corolock;

		std::atomic<WakeType> result;
	};

	struct PUBLICSHARE_API Awaiter
	{
		Awaiter(std::shared_ptr<CoTimer::Handle> handle);

		bool await_ready() const noexcept;
		void await_suspend(std::coroutine_handle<> coro);
		WakeType await_resume() noexcept;

		std::shared_ptr<CoTimer::Handle> handle;
		std::coroutine_handle<> coroutine;
	};

public:
	CoTimer(std::chrono::milliseconds timeout);

	CoTimer(CoTimer&& other) noexcept;
	CoTimer& operator=(CoTimer&& other) noexcept;

	CoTimer(const CoTimer&) = delete;
	CoTimer& operator=(const CoTimer&) = delete;

	~CoTimer();
	Awaiter operator co_await(); // 协程等待操作
	void wake();				 // 立即唤醒

private:
	std::shared_ptr<Handle> handle;
};

Task<bool> PUBLICSHARE_API CoSleep(std::chrono::milliseconds timeout);

inline bool CoCloseSocket(BaseSocket socket)
{
	if (socket <= 0)
		return false;
#ifdef __linux__
	return (close(socket) != -1);
#elif _WIN32
	return closesocket(socket) != SOCKET_ERROR;
#endif
}

// 协程连接器包装器
class PUBLICSHARE_API CoConnection
{
public:
	struct PUBLICSHARE_API Handle
	{
		Handle();
		~Handle();

		BaseSocket socket;
		sockaddr_in remoteaddr;

		bool active;

		std::coroutine_handle<> coroutine;
		std::atomic<bool> corodone;
		CriticalSectionLock corolock;
	};

	struct PUBLICSHARE_API Awaiter
	{
		Awaiter(std::shared_ptr<Handle> handle);

		bool await_ready();
		void await_suspend(std::coroutine_handle<> coro);
		BaseSocket await_resume();

		std::shared_ptr<Handle> handle;
		std::coroutine_handle<> coroutine;
	};

public:
	CoConnection(const std::string& ip, const int port);

	CoConnection(CoConnection&& other) noexcept;
	CoConnection& operator=(CoConnection&& other) noexcept;

	CoConnection(const CoConnection&) = delete;
	CoConnection& operator=(const CoConnection&) = delete;

	~CoConnection();
	Awaiter operator co_await();

private:
	std::shared_ptr<Handle> handle;
};

class PUBLICSHARE_API CoroCriticalSectionLock
{
public:
	CoroCriticalSectionLock();
	~CoroCriticalSectionLock();

public:
	bool try_lock();
	void lock();
	void unlock();

private:
	struct HolderStatus {
		enum class Type { None, Thread, Coroutine } type = Type::None;

		std::thread::id thread_id;	// 线程持有者
		CoroHandle coro_id;         // 协程持有者
		int recursive_count = 0;

	};

	CriticalSectionLock mutex;
	ConditionVariable cv;

	HolderStatus _status;
};
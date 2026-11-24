#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

// 统一的后台加载器：所有"读文件 / 解码 / 上传"类的加载都经过这里，同一个 key 同时只会执行一次。
//
//   - 去重：Request 同一个 key 时，如果已有请求在排队 / 执行中，直接返回那一个（导流到已有的请求）；
//           优先级取两者中较高的。完成后请求从表里移除，结果由调用方自己缓存（例如 AssetStroage 的资产表）。
//   - 调度：自己维护优先级队列（High > Normal > Low，同级先到先做），FlexThreadPool 只提供执行线程。
//           每个通道（Lane）可以单独限制同时执行的数量（缩略图默认最多 3 个），通道之间互不占用名额以外的线程。
//   - 等待：Wait() 时请求还在排队，就由当前线程直接领过来执行；已经在别的线程执行，就阻塞等它做完。
//           所以等依赖不会把线程池等死：要等的东西要么有人在做，要么自己做。
//   - 循环：A 等 B、B 又等 A 这种循环等待会被检测出来，Wait 直接返回 false（结果为空），不会卡死。
//
// 线程安全：所有接口可以在任意线程调用。

class FlexThreadPool;
struct AssetLoaderShared;

enum class AssetLoadPriority : uint8_t
{
	Low = 0,		// 缩略图、预览这类可以晚点再说的
	Normal = 1,		// 后台预取、依赖并行加载
	High = 2,		// 有人正在同步等它
};

enum class AssetLoadLane : uint8_t
{
	Asset = 0,		// 完整资产（进资产表）
	Thumbnail,		// 缩略图（不进资产表，限制并发数控制解码峰值内存）
	Count
};

class AssetLoadJob : public std::enable_shared_from_this<AssetLoadJob>
{
public:
	enum class State : uint8_t
	{
		Queued,		// 排队中
		Running,	// 某个线程正在执行
		Done,		// 执行完了（结果可能为空，例如文件不存在）
		Canceled,	// 排队时被取消，没有执行
	};

	virtual ~AssetLoadJob() = default;
	AssetLoadJob(const AssetLoadJob&) = delete;
	AssetLoadJob& operator=(const AssetLoadJob&) = delete;

	State GetState() const { return _state.load(std::memory_order_acquire); }
	bool IsFinished() const { const State s = GetState(); return s == State::Done || s == State::Canceled; }
	bool IsDone() const { return GetState() == State::Done; }
	const std::string& GetKey() const { return _key; }
	AssetLoadLane GetLane() const { return _lane; }
	AssetLoadPriority GetPriority() const { return static_cast<AssetLoadPriority>(_priority.load(std::memory_order_relaxed)); }

	// 等它完成。还在排队就在当前线程直接执行；别的线程在执行就阻塞等。
	// 返回 true 表示执行完了（结果用 GetResult 取）；被取消、或检测到循环等待时返回 false
	bool Wait();
	// 只能取消还在排队的；已经开始执行的取消不了（返回 false）
	bool Cancel();

protected:
	AssetLoadJob(std::string key, AssetLoadPriority priority, AssetLoadLane lane);
	virtual void Execute() = 0;
	// 不经过加载器、直接标成已完成（Completed 用）
	void MarkCompleted() { _state.store(State::Done, std::memory_order_release); }

private:
	void Finish(State finalState);

	std::string _key;
	std::atomic<State> _state{ State::Queued };
	std::atomic<uint8_t> _priority;
	AssetLoadLane _lane;
	uint64_t _sequence = 0;
	std::weak_ptr<AssetLoaderShared> _shared;

	std::mutex _doneMutex;
	std::condition_variable _doneCv;

	// 循环等待检测：执行它的线程（只在执行期间有效，读写都在等待图的锁下）
	void* _runner = nullptr;

	friend class AssetLoader;
	friend struct AssetLoaderShared;
};

template<typename T>
class AssetLoadTask final : public AssetLoadJob
{
public:
	AssetLoadTask(std::string key, AssetLoadPriority priority, AssetLoadLane lane, std::function<T()> work)
		: AssetLoadJob(std::move(key), priority, lane), _work(std::move(work))
	{}

	// 已经有结果时直接造一个完成状态的请求（例如资产已经在资产表里），调用方不用区分两种情况
	static std::shared_ptr<AssetLoadTask> Completed(T value, AssetLoadLane lane = AssetLoadLane::Asset)
	{
		auto task = std::make_shared<AssetLoadTask>(std::string(), AssetLoadPriority::Normal, lane, nullptr);
		task->_result = std::move(value);
		task->MarkCompleted();
		return task;
	}

	// 完成后才有值；没完成 / 被取消时返回默认值
	T GetResult() const { return IsDone() ? _result : T{}; }
	T WaitResult() { Wait(); return GetResult(); }

protected:
	void Execute() override
	{
		auto work = std::move(_work);
		_work = nullptr;
		if (!work)
			return;
		_result = RunGuarded(work);
	}

private:
	T RunGuarded(std::function<T()>& work);

	std::function<T()> _work;
	T _result{};
};

class AssetLoader
{
public:
	// workerCount = 0：最大线程数取 max(硬件并发数, 6)
	explicit AssetLoader(uint32_t workerCount = 0);
	~AssetLoader();

	AssetLoader(const AssetLoader&) = delete;
	AssetLoader& operator=(const AssetLoader&) = delete;

	// 提交一个加载请求。key 相同且还没完成的请求只会有一个：已有就返回已有的（work 不会执行）。
	// runInlineIfNew = true：确实是新请求时不进队列，直接在当前线程执行完再返回（同步加载用，避免交给别的线程再干等）
	template<typename T>
	std::shared_ptr<AssetLoadTask<T>> Request(const std::string& key, AssetLoadPriority priority, AssetLoadLane lane,
		std::function<T()> work, bool runInlineIfNew = false)
	{
		auto task = std::make_shared<AssetLoadTask<T>>(key, priority, lane, std::move(work));
		auto result = Submit(task, runInlineIfNew);
		if (auto typed = std::dynamic_pointer_cast<AssetLoadTask<T>>(result))
			return typed;
		// 同一个 key 被不同结果类型的请求占着：是调用方的 key 起名冲突。不去重，单独执行这一个
		ReportKeyTypeConflict(key);
		Submit(task, runInlineIfNew, false);
		return task;
	}

	// 某个通道同时最多执行几个（0 = 不限，按线程数）
	void SetLaneLimit(AssetLoadLane lane, uint32_t limit);
	// 取消所有还在排队的请求（例如切换工程）；正在执行的照常做完
	void CancelQueued();

	uint32_t GetWorkerCount() const { return _workerCount; }

private:
	std::shared_ptr<AssetLoadJob> Submit(const std::shared_ptr<AssetLoadJob>& job, bool runInlineIfNew, bool deduplicate = true);
	static void ReportKeyTypeConflict(const std::string& key);

	std::shared_ptr<AssetLoaderShared> _shared;
	std::unique_ptr<FlexThreadPool> _pool;
	uint32_t _workerCount = 0;
};

void AssetLoaderReportException(const std::string& key, const char* what);

template<typename T>
T AssetLoadTask<T>::RunGuarded(std::function<T()>& work)
{
	try
	{
		return work();
	}
	catch (const std::exception& e)
	{
		AssetLoaderReportException(GetKey(), e.what());
	}
	catch (...)
	{
		AssetLoaderReportException(GetKey(), "unknown exception");
	}
	return T{};
}

#include "Project/AssetLoader.h"

#include "FlexThreadPool.h"

#include <algorithm>
#include <format>
#include <iostream>
#include <optional>
#include <unordered_map>
#include <vector>

// ============================== 共享状态 ==============================

struct AssetLoaderShared
{
	std::mutex mutex;
	// 待执行的请求；被等待者领走 / 取消的不会马上删，挑选时顺手清掉
	std::vector<std::shared_ptr<AssetLoadJob>> queue;
	// key -> 还没完成的请求（排队中或执行中），完成后移除
	std::unordered_map<std::string, std::shared_ptr<AssetLoadJob>> inflight;

	uint32_t laneRunning[static_cast<size_t>(AssetLoadLane::Count)] = {};
	uint32_t laneLimit[static_cast<size_t>(AssetLoadLane::Count)] = {};

	uint32_t activePumps = 0;	// 在线程池里跑着的取任务循环数
	uint32_t maxPumps = 1;
	uint64_t nextSequence = 0;
	bool stopping = false;

	// 往线程池里投一个取任务循环（加载器析构时置空）
	std::function<void()> postPump;

	// ---- 以下都要求已经持有 mutex ----

	bool LaneHasRoom(AssetLoadLane lane) const
	{
		const size_t index = static_cast<size_t>(lane);
		return laneLimit[index] == 0 || laneRunning[index] < laneLimit[index];
	}

	// 从队列里拿掉指定的请求（被等待者领走、被取消时当场调用，不留给下一次挑选）
	void EraseFromQueueLocked(const AssetLoadJob* job)
	{
		auto it = std::find_if(queue.begin(), queue.end(), [job](const std::shared_ptr<AssetLoadJob>& item) { return item.get() == job; });
		if (it == queue.end())
			return;
		if (it != queue.end() - 1)
			*it = std::move(queue.back());
		queue.pop_back();
	}

	// 挑一个能执行的：优先级高的先，同级先到先做；通道满了的跳过
	std::shared_ptr<AssetLoadJob> PickLocked()
	{
		// 先清掉已经不在排队状态的（兜底：正常情况下领走 / 取消时已经当场删了）
		std::erase_if(queue, [](const std::shared_ptr<AssetLoadJob>& job) { return !job || job->GetState() != AssetLoadJob::State::Queued; });

		std::optional<size_t> best;
		uint8_t bestPriority = 0;
		uint64_t bestSequence = 0;
		for (size_t i = 0; i < queue.size(); ++i)
		{
			const auto& job = queue[i];
			if (!LaneHasRoom(job->_lane))
				continue;
			const uint8_t priority = job->_priority.load(std::memory_order_relaxed);
			if (!best || priority > bestPriority || (priority == bestPriority && job->_sequence < bestSequence))
			{
				best = i;
				bestPriority = priority;
				bestSequence = job->_sequence;
			}
		}
		if (!best)
			return nullptr;

		auto job = std::move(queue[*best]);
		if (*best != queue.size() - 1)
			queue[*best] = std::move(queue.back());
		queue.pop_back();

		job->_state.store(AssetLoadJob::State::Running, std::memory_order_release);
		++laneRunning[static_cast<size_t>(job->_lane)];
		return job;
	}

	void SpawnPumpIfNeededLocked()
	{
		if (stopping || !postPump || queue.empty() || activePumps >= maxPumps)
			return;
		++activePumps;
		postPump();
	}

	void RemoveInflightLocked(const std::shared_ptr<AssetLoadJob>& job)
	{
		auto it = inflight.find(job->_key);
		if (it != inflight.end() && it->second == job)
			inflight.erase(it);
	}

	static void Pump(const std::weak_ptr<AssetLoaderShared>& weak);
	static void Run(const std::shared_ptr<AssetLoaderShared>& shared, const std::shared_ptr<AssetLoadJob>& job);

	// 循环等待检测。返回 false：等下去会形成环（自己等自己，或者 A 等 B、B 又等 A）
	static bool BeginWait(AssetLoadJob* target);
	static void EndWait();
};

// ============================== 循环等待检测 ==============================

namespace
{
	// 每个线程：正在执行的请求栈（Wait 时就地执行会嵌套）、正在等的请求
	struct ThreadWaitRecord
	{
		std::vector<AssetLoadJob*> running;
		AssetLoadJob* waitingOn = nullptr;
	};
	thread_local ThreadWaitRecord t_waitRecord;

	// 等待图的锁：读写 job->_runner 和各线程的 waitingOn 都在它下面
	std::mutex& WaitGraphMutex()
	{
		static std::mutex mutex;
		return mutex;
	}

}

bool AssetLoaderShared::BeginWait(AssetLoadJob* target)
{
	std::lock_guard lock(WaitGraphMutex());
	auto& me = t_waitRecord;
	if (std::find(me.running.begin(), me.running.end(), target) != me.running.end())
		return false;

	// 沿着"执行它的线程在等谁"往下走，走回自己就是环
	AssetLoadJob* job = target;
	for (int depth = 0; job && depth < 1024; ++depth)
	{
		auto* runner = static_cast<ThreadWaitRecord*>(job->_runner);
		if (!runner)
			break;
		if (runner == &me)
			return false;
		job = runner->waitingOn;
	}
	me.waitingOn = target;
	return true;
}

void AssetLoaderShared::EndWait()
{
	std::lock_guard lock(WaitGraphMutex());
	t_waitRecord.waitingOn = nullptr;
}

// ============================== AssetLoaderShared ==============================

void AssetLoaderShared::Pump(const std::weak_ptr<AssetLoaderShared>& weak)
{
	for (;;)
	{
		auto shared = weak.lock();
		if (!shared)
			return;

		std::shared_ptr<AssetLoadJob> job;
		{
			std::lock_guard lock(shared->mutex);
			job = shared->stopping ? nullptr : shared->PickLocked();
			if (!job)
			{
				--shared->activePumps;
				return;
			}
		}
		Run(shared, job);
	}
}

void AssetLoaderShared::Run(const std::shared_ptr<AssetLoaderShared>& shared, const std::shared_ptr<AssetLoadJob>& job)
{
	{
		std::lock_guard lock(WaitGraphMutex());
		t_waitRecord.running.push_back(job.get());
		job->_runner = &t_waitRecord;
	}

	job->Execute();

	{
		std::lock_guard lock(WaitGraphMutex());
		job->_runner = nullptr;
		auto& running = t_waitRecord.running;
		if (!running.empty() && running.back() == job.get())
			running.pop_back();
		else
			running.erase(std::remove(running.begin(), running.end(), job.get()), running.end());
	}

	// 先标完成再从表里拿掉：中间来的同 key 请求拿到的是已完成的这一个，结果直接可用
	job->Finish(AssetLoadJob::State::Done);

	std::lock_guard lock(shared->mutex);
	--shared->laneRunning[static_cast<size_t>(job->_lane)];
	shared->RemoveInflightLocked(job);
	// 通道名额空出来了：之前因为通道满而没被领走的请求需要有人来领
	shared->SpawnPumpIfNeededLocked();
}

// ============================== AssetLoadJob ==============================

AssetLoadJob::AssetLoadJob(std::string key, AssetLoadPriority priority, AssetLoadLane lane)
	: _key(std::move(key)), _priority(static_cast<uint8_t>(priority)), _lane(lane)
{}

void AssetLoadJob::Finish(State finalState)
{
	{
		std::lock_guard lock(_doneMutex);
		_state.store(finalState, std::memory_order_release);
	}
	_doneCv.notify_all();
}

bool AssetLoadJob::Wait()
{
	State state = GetState();
	if (state == State::Done)
		return true;
	if (state == State::Canceled)
		return false;

	// 还在排队：自己领过来执行
	if (state == State::Queued)
	{
		auto shared = _shared.lock();
		bool claimed = false;
		if (shared)
		{
			std::lock_guard lock(shared->mutex);
			if (GetState() == State::Queued)
			{
				shared->EraseFromQueueLocked(this);
				_state.store(State::Running, std::memory_order_release);
				++shared->laneRunning[static_cast<size_t>(_lane)];
				claimed = true;
			}
		}
		if (claimed)
		{
			AssetLoaderShared::Run(shared, shared_from_this());
			return true;
		}
		if (!shared && GetState() == State::Queued)
			return false;	// 加载器已经没了，没人会执行它
	}

	// 别的线程在执行：阻塞等
	if (!AssetLoaderShared::BeginWait(this))
	{
		std::cerr << std::format("[ AssetLoader ] 检测到循环等待，放弃等待：{}\n", _key);
		return false;
	}
	{
		std::unique_lock lock(_doneMutex);
		_doneCv.wait(lock, [this] { return IsFinished(); });
	}
	AssetLoaderShared::EndWait();
	return GetState() == State::Done;
}

bool AssetLoadJob::Cancel()
{
	auto shared = _shared.lock();
	if (!shared)
		return false;
	{
		std::lock_guard lock(shared->mutex);
		if (GetState() != State::Queued)
			return false;
		shared->EraseFromQueueLocked(this);
		shared->RemoveInflightLocked(shared_from_this());
		_state.store(State::Canceled, std::memory_order_release);
	}
	Finish(State::Canceled);
	return true;
}

// ============================== AssetLoader ==============================

AssetLoader::AssetLoader(uint32_t workerCount)
	: _shared(std::make_shared<AssetLoaderShared>())
{
	// 最大线程数：取硬件并发数和 6 中较大的（FlexThreadPool 空闲的线程会自己退出，平时不会一直占着这么多）
	if (workerCount == 0)
		workerCount = std::max(std::thread::hardware_concurrency(), 6u);
	_workerCount = workerCount;

	_shared->maxPumps = workerCount;
	_shared->laneLimit[static_cast<size_t>(AssetLoadLane::Thumbnail)] = 3;

	_pool = std::make_unique<FlexThreadPool>(workerCount);
	_pool->start();

	FlexThreadPool* pool = _pool.get();
	std::weak_ptr<AssetLoaderShared> weak = _shared;
	_shared->postPump = [pool, weak]()
		{
			pool->submit([weak]() { AssetLoaderShared::Pump(weak); });
		};
}

AssetLoader::~AssetLoader()
{
	std::vector<std::shared_ptr<AssetLoadJob>> canceled;
	{
		std::lock_guard lock(_shared->mutex);
		_shared->stopping = true;
		_shared->postPump = nullptr;
		for (auto& job : _shared->queue)
		{
			if (job->GetState() == AssetLoadJob::State::Queued)
			{
				job->_state.store(AssetLoadJob::State::Canceled, std::memory_order_release);
				canceled.push_back(job);
			}
		}
		_shared->queue.clear();
		_shared->inflight.clear();
	}
	for (auto& job : canceled)
		job->Finish(AssetLoadJob::State::Canceled);

	// 正在执行的照常做完
	if (_pool)
		_pool->stop();
	_pool.reset();
}

std::shared_ptr<AssetLoadJob> AssetLoader::Submit(const std::shared_ptr<AssetLoadJob>& job, bool runInlineIfNew, bool deduplicate)
{
	bool runNow = false;
	{
		std::lock_guard lock(_shared->mutex);
		if (_shared->stopping)
		{
			job->_state.store(AssetLoadJob::State::Canceled, std::memory_order_release);
			return job;
		}

		if (deduplicate && !job->_key.empty())
		{
			auto it = _shared->inflight.find(job->_key);
			if (it != _shared->inflight.end() && it->second->GetState() != AssetLoadJob::State::Canceled)
			{
				// 导流到已有的请求；新来的更急就把它提上去
				auto& existing = it->second;
				uint8_t current = existing->_priority.load(std::memory_order_relaxed);
				const uint8_t wanted = job->_priority.load(std::memory_order_relaxed);
				while (wanted > current && !existing->_priority.compare_exchange_weak(current, wanted, std::memory_order_relaxed)) {}
				return existing;
			}
			_shared->inflight[job->_key] = job;
		}

		job->_shared = _shared;
		job->_sequence = _shared->nextSequence++;

		if (runInlineIfNew)
		{
			job->_state.store(AssetLoadJob::State::Running, std::memory_order_release);
			++_shared->laneRunning[static_cast<size_t>(job->_lane)];
			runNow = true;
		}
		else
		{
			_shared->queue.push_back(job);
			_shared->SpawnPumpIfNeededLocked();
		}
	}

	if (runNow)
		AssetLoaderShared::Run(_shared, job);
	return job;
}

void AssetLoader::SetLaneLimit(AssetLoadLane lane, uint32_t limit)
{
	std::lock_guard lock(_shared->mutex);
	_shared->laneLimit[static_cast<size_t>(lane)] = limit;
	_shared->SpawnPumpIfNeededLocked();
}

void AssetLoader::CancelQueued()
{
	std::vector<std::shared_ptr<AssetLoadJob>> canceled;
	{
		std::lock_guard lock(_shared->mutex);
		for (auto& job : _shared->queue)
		{
			if (job->GetState() == AssetLoadJob::State::Queued)
			{
				job->_state.store(AssetLoadJob::State::Canceled, std::memory_order_release);
				_shared->RemoveInflightLocked(job);
				canceled.push_back(job);
			}
		}
		_shared->queue.clear();
	}
	for (auto& job : canceled)
		job->Finish(AssetLoadJob::State::Canceled);
}

void AssetLoader::ReportKeyTypeConflict(const std::string& key)
{
	std::cerr << std::format("[ AssetLoader ] 请求 key 冲突（同一个 key 被不同结果类型的请求使用）：{}\n", key);
}

void AssetLoaderReportException(const std::string& key, const char* what)
{
	std::cerr << std::format("[ AssetLoader ] 加载任务抛出异常{}：{}\n", key.empty() ? std::string() : std::format("（{}）", key), what ? what : "");
}

#pragma once

#include "CriticalSectionLock.h"
#include <map>
#include <queue>
#include <functional>
#include <iostream>
#include <unordered_map>
#include <set>

class CoroCriticalSectionLock;

template<typename T>
concept CoLockable = std::is_same_v<std::remove_cvref_t<T>, CoroCriticalSectionLock>;

template <typename K, typename V, Lockable Mutex = CriticalSectionLock>
class SafeMap
{
public:
	SafeMap() {}
	SafeMap(const SafeMap& other)
	{
		LockGuard lock(other._lock);
		_map = other._map;
	}
	SafeMap(SafeMap&& other)
	{
		LockGuard lock(other._lock);
		_map = std::move(other._map);
	}
	SafeMap& operator=(const SafeMap& other)
	{
		if (this == &other)
			return *this;
		LockGuard guard1(_lock);
		LockGuard guard2(other._lock);
		_map = other._map;
		return *this;
	}
	SafeMap& operator=(SafeMap&& other)
	{
		if (this == &other)
			return *this;
		LockGuard guard1(_lock);
		LockGuard guard2(other._lock);
		_map = std::move(other._map);
		return *this;
	}
	~SafeMap() {}

	V &operator[](const K &key)
	{
		LockGuard guard(_lock);
		return _map[key];
	}

	int Size() const
	{
		LockGuard guard(_lock);
		return _map.size();
	}

	bool IsEmpty() const
	{
		LockGuard guard(_lock);
		return _map.empty();
	}

	bool Insert(const K &key, const V &value)
	{
		LockGuard guard(_lock);
		if (_map.find(key) != _map.end())
		{
			return false;
		}
		auto ret = _map.insert(std::pair<K, V>(key, value));
		return true;
	}

	void EnsureInsert(const K &key, const V &value)
	{
		LockGuard guard(_lock);
		auto ret = _map.insert(std::pair<K, V>(key, value));
		// find key and cannot insert
		if (!ret.second)
		{
			_map.erase(ret.first);
			_map.insert(std::pair<K, V>(key, value));
			return;
		}
		return;
	}

	bool Exist(const K &key) const
	{
		LockGuard guard(_lock);
		auto iter = _map.find(key);
		return iter != _map.end();
	}

	bool Find(const K &key, V &value)
	{
		LockGuard guard(_lock);

		auto iter = _map.find(key);
		if (iter != _map.end())
		{
			value = iter->second;
			return true;
		}

		return false;
	}

	bool FindOldAndSetNew(const K &key, V &oldValue, const V &newValue)
	{
		LockGuard guard(_lock);

		if (_map.size() > 0)
		{
			auto iter = _map.find(key);
			if (iter != _map.end())
			{
				oldValue = iter->second;
				_map.erase(iter);
				_map.insert(std::pair<K, V>(key, newValue));
				return true;
			}
		}

		return false;
	}

	void Erase(const K &key)
	{
		LockGuard guard(_lock);
		_map.erase(key);
	}

	void Clear()
	{
		LockGuard guard(_lock);
		_map.clear();
		return;
	}

	std::vector<K> GetKeys() const
	{
		std::vector<K> keys;
		LockGuard guard(_lock);
		keys.reserve(_map.size());
		for (const auto &[key, value] : _map)
			keys.push_back(key);
		return keys;
	}

	std::vector<V> GetValues() const
	{
		std::vector<V> values;
		LockGuard guard(_lock);
		values.reserve(_map.size());
		for (const auto& [key, value] : _map)
			values.push_back(value);
		return values;
	}

	using SyncCallback = std::function<void(std::map<K,V>& map)>;
	void EnsureCall(SyncCallback&& callback)
	{
		if (callback)
		{
			LockGuard guard(_lock);
			callback(this->_map);
		}
	}

	auto MakeLockGuard() const
	{
		return LockGuard(_lock);
	}

private:
	std::map<K, V> _map;
	mutable Mutex _lock;
};

template <typename K, typename V, Lockable Mutex = CriticalSectionLock>
class SafeUnorderedMap
{
public:
	SafeUnorderedMap() {}
	SafeUnorderedMap(const SafeUnorderedMap& other)
	{
		LockGuard lock(other._lock);
		_map = other._map;
	}
	SafeUnorderedMap(SafeUnorderedMap&& other)
	{
		LockGuard lock(other._lock);
		_map = std::move(other._map);
	}
	SafeUnorderedMap& operator=(const SafeUnorderedMap& other)
	{
		if (this == &other)
			return *this;
		LockGuard guard1(_lock);
		LockGuard guard2(other._lock);
		_map = other._map;
		return *this;
	}
	SafeUnorderedMap& operator=(SafeUnorderedMap&& other)
	{
		if (this == &other)
			return *this;
		LockGuard guard1(_lock);
		LockGuard guard2(other._lock);
		_map = std::move(other._map);
		return *this;
	}
	~SafeUnorderedMap() {}

	V &operator[](const K &key)
	{
		LockGuard guard(_lock);
		return _map[key];
	}

	int Size() const
	{
		LockGuard guard(_lock);
		return _map.size();
	}

	bool IsEmpty() const
	{
		LockGuard guard(_lock);
		return _map.empty();
	}

	bool Insert(const K &key, const V &value)
	{
		LockGuard guard(_lock);
		if (_map.find(key) != _map.end())
		{
			return false;
		}
		auto ret = _map.insert(std::pair<K, V>(key, value));
		return true;
	}

	void EnsureInsert(const K &key, const V &value)
	{
		LockGuard guard(_lock);
		auto ret = _map.insert(std::pair<K, V>(key, value));
		// find key and cannot insert
		if (!ret.second)
		{
			_map.erase(ret.first);
			_map.insert(std::pair<K, V>(key, value));
			return;
		}
		return;
	}

	bool Exist(const K &key) const
	{
		LockGuard guard(_lock);
		auto iter = _map.find(key);
		return iter != _map.end();
	}

	bool Find(const K &key, V &value)
	{
		LockGuard guard(_lock);

		auto iter = _map.find(key);
		if (iter != _map.end())
		{
			value = iter->second;
			return true;
		}

		return false;
	}

	bool FindOldAndSetNew(const K &key, V &oldValue, const V &newValue)
	{
		LockGuard guard(_lock);

		if (_map.size() > 0)
		{
			auto iter = _map.find(key);
			if (iter != _map.end())
			{
				oldValue = iter->second;
				_map.erase(iter);
				_map.insert(std::pair<K, V>(key, newValue));
				return true;
			}
		}

		return false;
	}

	void Erase(const K &key)
	{
		LockGuard guard(_lock);
		_map.erase(key);
	}

	void Clear()
	{
		LockGuard guard(_lock);
		_map.clear();
		return;
	}

	std::vector<K> GetKeys() const
	{
		std::vector<K> keys;
		LockGuard guard(_lock);
		keys.reserve(_map.size());
		for (const auto &[key, value] : _map)
			keys.push_back(key);
		return keys;
	}

	std::vector<V> GetValues() const
	{
		std::vector<V> values;
		LockGuard guard(_lock);
		values.reserve(_map.size());
		for (const auto& [key, value] : _map)
			values.push_back(value);
		return values;
	}

	using SyncCallback = std::function<void(std::unordered_map<K,V>& map)>;
	void EnsureCall(SyncCallback&& callback)
	{
		if (callback)
		{
			LockGuard guard(_lock);
			callback(this->_map);
		}
	}

	auto MakeLockGuard() const
	{
		return LockGuard(_lock);
	}

private:
	std::unordered_map<K, V> _map;
	mutable Mutex _lock;
};

template <typename T, Lockable Mutex = CriticalSectionLock>
class SafeQueue
{
private:
	std::queue<T> _queue;
	mutable Mutex _lock;

public:
	SafeQueue() {}
	SafeQueue(const SafeQueue& other)
	{
		LockGuard lock(other._lock);
		_queue = other._queue;
	}
	SafeQueue(SafeQueue&& other)
	{
		LockGuard lock(other._lock);
		_queue = std::move(other._queue);
	}
	SafeQueue& operator=(const SafeQueue& other)
	{
		if (this == &other)
			return *this;
		LockGuard guard1(_lock);
		LockGuard guard2(other._lock);
		_queue = other._queue;
		return *this;
	}
	SafeQueue& operator=(SafeQueue&& other)
	{
		if (this == &other)
			return *this;
		LockGuard guard1(_lock);
		LockGuard guard2(other._lock);
		_queue = std::move(other._queue);
		return *this;
	}
	~SafeQueue() {}
	bool empty() const
	{
		LockGuard guard(_lock);
		bool result = _queue.empty();
		return result;
	}
	int size() const
	{
		LockGuard guard(_lock);
		int result = _queue.size();
		return result;
	}
	// 队列添加元素
	void enqueue(T &t)
	{
		LockGuard guard(_lock);
		_queue.emplace(t);
	}
	void enqueue(T &&t)
	{
		LockGuard guard(_lock);
		_queue.push(std::forward<T>(t));
	}
	// 队列取出元素
	bool dequeue(T &t)
	{
		LockGuard guard(_lock);
		if (_queue.empty())
		{
			return false;
		}
		t = std::move(_queue.front());
		_queue.pop();
		return true;
	}
	// 查看队列首元素
	bool front(T &t)
	{
		LockGuard guard(_lock);
		if (_queue.empty())
		{
			return false;
		}
		t = _queue.front();
		return true;
	}
	// 查看队列尾元素
	bool back(T &t)
	{
		LockGuard guard(_lock);
		if (_queue.empty())
		{
			return false;
		}
		t = _queue.back();
		return true;
	}
	void clear()
	{
		LockGuard guard(_lock);
		while (!_queue.empty())
			_queue.pop();
	}
	using SyncCallback = std::function<void(std::queue<T>& deque)>;
	void EnsureCall(SyncCallback&& callback)
	{
		if (callback)
		{
			LockGuard guard(_lock);
			callback(this->_deque);
		}
	}

	auto MakeLockGuard() const
	{
		return LockGuard(_lock);
	}
};

template <typename T, Lockable Mutex = CriticalSectionLock>
class SafeArray
{
private:
	std::vector<T> _array;
	mutable Mutex _lock;

public:
	SafeArray() {}
	SafeArray(const SafeArray &other)
	{
		LockGuard guard(other._lock);
		_array = other._array;
	}
	SafeArray(SafeArray &&other)
	{
		LockGuard guard(other._lock);
		_array = std::move(other._array);
	}
	SafeArray &operator=(const SafeArray &other)
	{
		if (this == &other)
			return *this;
		LockGuard guard1(_lock);
		LockGuard guard2(other._lock);
		_array = other._array;
		return *this;
	}
	SafeArray& operator=(SafeArray&& other)
	{
		if (this == &other)
			return *this;
		LockGuard guard1(_lock);
		LockGuard guard2(other._lock);
		_array = std::move(other._array);
		return *this;
	}
	~SafeArray() {}
	void clear()
	{
		LockGuard guard(_lock);
		_array.clear();
	}
	bool empty() const
	{
		LockGuard guard(_lock);
		bool result = _array.empty();
		return result;
	}
	int size() const
	{
		LockGuard guard(_lock);
		int result = _array.size();
		return result;
	}
	// 数组添加元素
	template <typename... Args>
	void emplace(Args &&...args)
	{
		LockGuard guard(_lock);
		_array.emplace_back(std::forward<Args>(args)...);
	}

	// 数组访问元素
	bool getIndexElement(size_t index, T &t)
	{
		LockGuard guard(_lock);
		if (_array.empty() || index >= _array.size())
		{
			return false;
		}
		t = _array.at(index);
		return true;
	}
	// 数组删除元素
	bool deleteIndexElement(size_t index)
	{
		LockGuard guard(_lock);
		if (_array.empty() || index >= _array.size())
		{
			return false;
		}
		_array.erase(_array.begin() + index);
		return true;
	}

	T &operator[](size_t index)
	{
		LockGuard guard(_lock);
		if (index >= _array.size())
		{
			throw std::out_of_range("SafeArray index out of range");
		}
		return _array[index];
	}

	const T &operator[](size_t index) const
	{
		LockGuard guard(_lock);
		if (index >= _array.size())
		{
			throw std::out_of_range("SafeArray index out of range");
		}
		return _array[index];
	}

	using SyncCallback = std::function<void(std::vector<T>& array)>;
	void EnsureCall(SyncCallback&& callback)
	{
		if (callback)
		{
			LockGuard guard(_lock);
			callback(this->_array);
		}
	}

	auto MakeLockGuard() const
	{
		return LockGuard(_lock);
	}
};

template <typename T, Lockable Mutex = CriticalSectionLock>
class SafeDeQue
{
private:
	std::deque<T> _deque;
	mutable Mutex _lock;

public:
	SafeDeQue() {}
	SafeDeQue(const SafeDeQue& other)
	{
		LockGuard guard(other._lock);
		_deque = other._deque;
	}
	SafeDeQue(SafeDeQue&& other)
	{
		LockGuard guard(other._lock);
		_deque = std::move(other._deque);
	}
	SafeDeQue& operator=(const SafeDeQue& other)
	{
		if (this == &other)
			return *this;
		LockGuard guard1(_lock);
		LockGuard guard2(other._lock);
		_deque = other._deque;
		return *this;
	}
	SafeDeQue& operator=(SafeDeQue&& other)
	{
		if (this == &other)
			return *this;
		LockGuard guard1(_lock);
		LockGuard guard2(other._lock);
		_deque = std::move(other._deque);
		return *this;
	}
	~SafeDeQue(){}

	bool empty() const
	{
		LockGuard guard(_lock);
		bool result = _deque.empty();
		return result;
	}
	int size() const
	{
		LockGuard guard(_lock);
		int result = _deque.size();
		return result;
	}
	// 队尾添加元素
	void enqueue_back(T &t)
	{
		LockGuard guard(_lock);
		_deque.emplace_back(t);
	}
	// 队首添加元素
	void enqueue_front(T &t)
	{
		LockGuard guard(_lock);
		_deque.emplace_front(t);
	}
	// 队首取出元素
	bool dequeue_front(T &t)
	{
		LockGuard guard(_lock);
		if (_deque.empty())
		{
			return false;
		}
		t = std::move(_deque.front());
		_deque.pop_front();
		return true;
	}
	// 查看队列首元素
	bool front(T &t)
	{
		LockGuard guard(_lock);
		if (_deque.empty())
		{
			return false;
		}
		t = _deque.front();
		return true;
	}
	// 查看队列尾元素
	bool back(T &t)
	{
		LockGuard guard(_lock);
		if (_deque.empty())
		{
			return false;
		}
		t = _deque.back();
		return true;
	}
	void clear()
	{
		LockGuard guard(_lock);
		_deque.clear();
	}

	using SyncCallback = std::function<void(std::deque<T>& deque)>;
	void EnsureCall(SyncCallback&& callback)
	{
		if (callback)
		{
			LockGuard guard(_lock);
			callback(this->_deque);
		}
	}

	auto MakeLockGuard() const
	{
		return LockGuard(_lock);
	}
};

template <typename T, Lockable Mutex = CriticalSectionLock>
class SafeSet
{
public:
	SafeSet() {}
	SafeSet(const SafeSet& other)
	{
		LockGuard guard(other._lock);
		_set = other._set;
	}
	SafeSet(SafeSet&& other)
	{
		LockGuard guard(other._lock);
		_set = std::move(other._set);
	}
	SafeSet& operator=(const SafeSet& other)
	{
		if (this == &other)
			return *this;
		LockGuard guard1(_lock);
		LockGuard guard2(other._lock);
		_set = other._set;
		return *this;
	}
	SafeSet& operator=(SafeSet&& other)
	{
		if (this == &other)
			return *this;
		LockGuard guard1(_lock);
		LockGuard guard2(other._lock);
		_set = std::move(other._set);
		return *this;
	}
	~SafeSet() {}

	int Size() const
	{
		LockGuard guard(_lock);
		return _set.size();
	}

	bool IsEmpty() const
	{
		LockGuard guard(_lock);
		return _set.empty();
	}

	bool Insert(const T &value)
	{
		LockGuard guard(_lock);
		if (_set.find(value) != _set.end())
			return false;

		_set.insert(value);
		return true;
	}

	bool Exist(const T &value) const
	{
		LockGuard guard(_lock);
		return _set.find(value) != _set.end();
	}

	void Erase(const T &value)
	{
		LockGuard guard(_lock);
		_set.erase(value);
	}

	void Clear()
	{
		LockGuard guard(_lock);
		_set.clear();
	}

	using SyncCallback = std::function<void(std::set<T>& set)>;
	void EnsureCall(SyncCallback&& call)
	{
		if (call)
		{
			LockGuard guard(_lock);
			call(this->_set);
		}
	}

	auto MakeLockGuard() const
	{
		return LockGuard(_lock);
	}

private:
	std::set<T> _set;
	mutable Mutex _lock;
};
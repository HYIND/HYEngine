#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>
#include "CriticalSectionLock.h"
#include "SpinLock.h"

template<typename T, typename Lockable = SpinLock>
class SharedObjectPool
{
	struct State
	{
		explicit State(std::size_t maxCachedCount) : _maxCachedCount(maxCachedCount) {}

		mutable Lockable _mutex;
		std::vector<T*> _available;
		const std::size_t _maxCachedCount;

		~State()
		{
			for (T* object : _available)
				delete object;
		}
	};

public:
	SharedObjectPool(const SharedObjectPool&) = delete;
	SharedObjectPool& operator=(const SharedObjectPool&) = delete;

	explicit SharedObjectPool(std::size_t maxCachedCount = 128)
		: _state(std::make_shared<State>(maxCachedCount))
	{
	}

	template<typename... Args>
	std::shared_ptr<T> Acquire(Args&&... args)
	{
		static_assert(std::is_assignable_v<T&, T&&>, "Pooled types must support move assignment");

		T* object = nullptr;
		{
			LockGuard guard(_state->_mutex);
			if (!_state->_available.empty())
			{
				object = _state->_available.back();
				_state->_available.pop_back();
			}
		}

		if (object)
		{
			try
			{
				//*object = T(std::forward<Args>(args)...);
				new (object) T(std::forward<Args>(args)...);
			}
			catch (...)
			{
				delete object;
				throw;
			}
		}
		else
		{
			object = new T(std::forward<Args>(args)...);
		}

		auto state = _state;
		return std::shared_ptr<T>(object, [state = std::move(state)](T* returned) noexcept
		{
			bool cached = false;
			try
			{
				{
					LockGuard guard(state->_mutex);
					if (state->_available.size() < state->_maxCachedCount)
					{
						state->_available.push_back(returned);
						cached = true;
					}
				}
			}
			catch (...)
			{
			}
			if (!cached)
				delete returned;
		});
	}

private:
	std::shared_ptr<State> _state;
};

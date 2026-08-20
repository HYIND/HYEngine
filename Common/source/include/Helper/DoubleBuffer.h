#pragma once

#include <vector>
#include "Coroutine.h"

template<typename DataType, typename Lockable = CoroCriticalSectionLock>
class DoubleBuffer
{
	template<typename T, typename = void>
	struct has_dot_reset : std::false_type {};

	template<typename T>
	struct has_dot_reset<T, std::void_t<decltype(std::declval<T>().reset())>> : std::true_type {};

	// 检查是否有 ->reset() 方法
	template<typename T, typename = void>
	struct has_arrow_reset : std::false_type {};

	template<typename T>
	struct has_arrow_reset<T, std::void_t<decltype(std::declval<T>()->reset())>> : std::true_type {};

public:
	DoubleBuffer()
	{
		_datas.resize(2);
	}

	DoubleBuffer(const DataType& initValue)
	{
		_datas.resize(2);
		for (auto& data : _datas) {
			data = initValue;
		}
	}

	void setInitialValue(int index, const DataType& value) {
		if (index >= 2 || index < 0)
			return;

		LockGuard guard(_mutex);
		_datas[index] = value;
	}
	void setInitialValue(int index, DataType&& value) {
		if (index >= 2 || index < 0)
			return;

		LockGuard guard(_mutex);
		_datas[index] = value;
	}

	DataType& acquireWriteBuffer() {
		return _datas[_writeIndex];
	}
	void submitWriteBuffer() {// 提交已完成的帧数据
		LockGuard guard(_mutex);
		_cv_producer.Wait(guard, [this]() {
			return !_read_locked;
			});
		std::swap(_writeIndex, _readIndex);
		_read_locked = true;
		_cv_consumer.NotifyOne();
	}

	DataType& acquireReadBuffer() {// 获取可读的缓冲区
		LockGuard guard(_mutex);
		_cv_consumer.Wait(guard, [this]() {	// 如果写端没准备好,则阻塞等待
			return _read_locked;
			});
		return _datas[_readIndex];
	}

	void ReleaseReadBuffer() {// 释放读缓冲区（标记为"已消费完，可被写端交换"）
		LockGuard guard(_mutex);
		clearData(_datas[_readIndex]);
		_read_locked = false;
		_cv_producer.NotifyOne();
	}

	void clearData(DataType& data) {
		if constexpr (has_arrow_reset<DataType>::value) {
			if (data) {
				data->reset();
			}
		}
		else if constexpr (has_dot_reset<DataType>::value) {
			data.reset();
		}
	}

private:
	// 2个缓冲区
	std::vector<DataType> _datas;

	int _writeIndex = 0;    // 写索引
	int _readIndex = 1;     // 读索引

	// 同步
	mutable Lockable _mutex;
	bool _read_locked = false;	// 前台读数据是否被锁定

	ConditionVariable _cv_producer;
	ConditionVariable _cv_consumer;
};
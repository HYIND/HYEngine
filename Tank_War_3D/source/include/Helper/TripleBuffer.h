#pragma once

#include <vector>
#include "Coroutine.h"

template<typename DataType>
class TripleBuffer
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
	TripleBuffer()
	{
		_datas.resize(3);
	}

	TripleBuffer(const DataType& initValue)
	{
		_datas.resize(3);
		for (auto& data : _datas) {
			data = initValue;
		}
	}

	void setInitialValue(int index, const DataType& value) {
		if (index >= 3 || index < 0)
			return;

		LockGuard guard(_mutex);
		_datas[index] = value;
	}
	void setInitialValue(int index, DataType&& value) {
		if (index >= 3 || index < 0)
			return;

		LockGuard guard(_mutex);
		_datas[index] = value;
	}


	DataType& acquireWriteBuffer() {// 获取可写的缓冲区
		return _datas[_writeIndex];
	}
	void submitWriteBuffer() {// 提交已完成的帧数据
		LockGuard guard(_mutex);
		std::swap(_writeIndex, _readyIndex);
		_dataReady = true;
		_datasSubmitted++;
		clearData(_datas[_writeIndex]);
	}


	DataType& acquireReadBuffer() {// 获取可读的缓冲区
		if (!_dataReady)
			return _datas[_readIndex];

		LockGuard guard(_mutex);
		if (!_dataReady)
			return _datas[_readIndex];

		clearData(_datas[_readIndex]);
		std::swap(_readyIndex, _readIndex);
		_dataReady = false;
		_datasUsed++;

		return _datas[_readIndex];
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

	// ==================== 统计信息 ====================
	struct Status {
		uint64_t submitted;		// 已提交的数据
		uint64_t Used;			// 已使用的数据
		int currentReadIndex;   // 当前读取索引
		int currentWriteIndex;  // 当前写入索引
	};
	Status getStatus() const
	{
		return Status{
			.submitted = _datasSubmitted.load(),
			.Used = _datasUsed.load(),
			.currentReadIndex = _readIndex,
			.currentWriteIndex = _writeIndex
		};
	}

private:
	// 三个缓冲区
	std::vector<DataType> _datas;

	int _writeIndex = 0;    // 可写
	int _readyIndex = 1;    // 准备好的
	int _readIndex = 2;     // 可读

	// 同步
	CoroCriticalSectionLock _mutex;
	std::atomic<bool> _dataReady = false;

	// 统计
	std::atomic<uint64_t> _datasSubmitted{ 0 };
	std::atomic<uint64_t> _datasUsed{ 0 };

};
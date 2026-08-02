#pragma once

#include <list>
#include <vector>
#include "CriticalSectionLock.h"

template <class DataType, Lockable Mutex = CriticalSectionLock>
class ResPool
{
protected:
	Mutex _lock;
	std::list<DataType *> _iDleList;
	std::vector<DataType *> _datas;
	uint32_t _maxResNum = 1000;

public:
	ResPool(uint32_t InitResNum = 300, uint32_t maxResNum = 1000)
	{
		LockGuard lock(_lock);

		_maxResNum = maxResNum;

		_iDleList.clear();
		_datas.clear();

		for (size_t i = 0; i < InitResNum; i++)
		{
			DataType *data = new DataType();
			_iDleList.push_back(data);
			_datas.push_back(data);
		}
	}

	~ResPool()
	{
		LockGuard lock(_lock);
		_datas.clear();
		for (auto it = _iDleList.begin(); it != _iDleList.end(); it++)
		{
			delete (*it);
		}
		_iDleList.clear();
	}

	// 分配空间
	virtual DataType *AllocateData()
	{
		DataType *data = nullptr;

		LockGuard lock(_lock);
		if (_iDleList.size() > 0) // list不为空，从list中取一个
		{
			data = _iDleList.front();
			_iDleList.pop_front();
		}
		else // list为空，新建一个
		{
			data = new DataType();
			if (_datas.size() < _maxResNum)
				_datas.push_back(data);
		}

		return data;
	}

	// 回收
	void ReleaseData(DataType *data)
	{
		LockGuard lock(_lock);

		// 已持有的数据
		auto it = find(_datas.begin(), _datas.end(), data);
		if (it != _datas.end())
		{
			if (std::find(_iDleList.begin(), _iDleList.end(), data) == _iDleList.end())
			{
				ResetData(data);
				_iDleList.push_back(data);
			}
			else
			{
				std::cerr << "ResPool::ReleaseData double free!!!\n";
			}
		}
		else // 外部数据，非池子持有的
		{
			if (_datas.size() < _maxResNum)
			{
				ResetData(data);
				_datas.push_back(data);
				_iDleList.push_back(data);
			}
			else
			{
				SAFE_DELETE(data);
			}
		}
	}

	virtual void ResetData(DataType *data) {}
};
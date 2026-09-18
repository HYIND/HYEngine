#include <algorithm>
#include <execution>
#include "SegmentBufferManager.h"

inline static uint32_t align_up(uint64_t value, uint64_t alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

template<typename BufferImpl, typename SegmentID>
SegmentBufferManager<BufferImpl, SegmentID>::SegmentBufferManager(uint64_t initsize)
	:_useSpace(0)
{
	_buffer = std::make_unique<BufferImpl>();
	_buffer->ReSize(initsize);
}
template<typename BufferImpl, typename SegmentID>
SegmentBufferManager<BufferImpl, SegmentID>::SegmentBufferManager(std::unique_ptr<BufferImpl>&& buffer)
	:_useSpace(0)
{
	_buffer = std::move(buffer);
}

template<typename BufferImpl, typename SegmentID>
bool SegmentBufferManager<BufferImpl, SegmentID>::AddSegment(const SegmentID& id, void* userData, const void* mem, size_t length, SegmentData& data)
{
	auto it = _usingSegment.find(id);
	if (it != _usingSegment.end())
		return false;

	uint64_t alignSize = align_up(length, _align);

	Segment seg;
	if (!FetchIdleSegment(alignSize, seg))
	{
		seg.first = _useSpace;
		seg.count = alignSize;
		_useSpace += alignSize;
	}
	_usingSegment[id] = seg;
	_userDatas[id] = userData;
	_buffer->WriteData(mem, seg.first, length);

	data.userData = userData;
	data.first = seg.first;
	data.count = seg.count;
	return true;
}

template<typename BufferImpl, typename SegmentID>
SegmentData SegmentBufferManager<BufferImpl, SegmentID>::SetSegment(const SegmentID& id, void* userData, const void* mem, size_t length)
{
	_userDatas[id] = userData;

	uint64_t alignSize = align_up(length, _align);

	auto it = _usingSegment.find(id);
	if (it != _usingSegment.end())
	{
		auto oriSegment = it->second;
		if (oriSegment.count >= alignSize)
		{
			_buffer->WriteData(mem, oriSegment.first, length);
			return SegmentData{ userData, oriSegment.first, alignSize };
		}
		else
		{	//空间不足，移动
			auto newSegment = MoveSegment(id, alignSize);
			_buffer->WriteData(mem, newSegment.first, length);
			return SegmentData{ userData, newSegment.first, alignSize };
		}
	}
	else
	{
		Segment seg;
		if (!FetchIdleSegment(alignSize, seg))
		{
			seg.first = _useSpace;
			seg.count = alignSize;
			_useSpace += alignSize;
		}
		_usingSegment[id] = seg;
		_buffer->WriteData(mem, seg.first, length);

		return SegmentData{ userData, seg.first, seg.count };
	}
}

template<typename BufferImpl, typename SegmentID>
bool SegmentBufferManager<BufferImpl, SegmentID>::FindSegment(const SegmentID& id, SegmentData& data) const
{
	{
		auto it = _usingSegment.find(id);
		if (it == _usingSegment.end())
			return false;
		data.first = it->second.first;
		data.count = it->second.count;
	}

	{
		auto it = _userDatas.find(id);
		data.userData = (it != _userDatas.end()) ? it->second : nullptr;
	}

	return true;
}

template<typename BufferImpl, typename SegmentID>
bool SegmentBufferManager<BufferImpl, SegmentID>::IsSegmentExist(const SegmentID& id)
{
	return _usingSegment.find(id) != _usingSegment.end();
}

template<typename BufferImpl, typename SegmentID>
bool SegmentBufferManager<BufferImpl, SegmentID>::RemoveSegment(const SegmentID& id, SegmentData& data)
{
	auto it = _usingSegment.find(id);
	if (it == _usingSegment.end())
		return false;

	auto seg = it->second;
	_usingSegment.erase(it);

	_idleSegment.push_back(seg);
	SortAndMegerIdleSegment();

	auto it_data = _userDatas.find(id);
	if (it_data != _userDatas.end())
	{
		data.userData = (*it_data).second;
		_userDatas.erase(it_data);
	}

	data.first = seg.first;
	data.count = seg.count;
	return true;
}

template<typename BufferImpl, typename SegmentID>
bool SegmentBufferManager<BufferImpl, SegmentID>::RemoveSegment(const SegmentID& id)
{
	auto it = _usingSegment.find(id);
	if (it == _usingSegment.end())
		return false;

	auto seg = it->second;
	_usingSegment.erase(it);

	_idleSegment.push_back(seg);
	SortAndMegerIdleSegment();

	auto it_data = _userDatas.find(id);
	if (it_data != _userDatas.end())
		_userDatas.erase(it_data);

	return true;
}

template<typename BufferImpl, typename SegmentID>
void SegmentBufferManager<BufferImpl, SegmentID>::SortAndMegerIdleSegment()
{
	std::sort(std::execution::par_unseq, _idleSegment.begin(), _idleSegment.end(),
		[](const Segment& v1, const Segment& v2)
		{return v1.first < v2.first; }
	);

	if (_idleSegment.size() > 1)
	{
		for (auto it = _idleSegment.begin(); it != (_idleSegment.end() - 1);)
		{
			auto& cur = *it;
			auto& next = *(it + 1);
			if (cur.first + cur.count == next.first)
			{
				next.first = cur.first;
				next.count = cur.count + next.count;
				it = _idleSegment.erase(it);
			}
			else
				it++;
		}
	}
}

template<typename BufferImpl, typename SegmentID>
bool SegmentBufferManager<BufferImpl, SegmentID>::FetchIdleSegment(size_t length, Segment& seg)
{
	for (auto it = _idleSegment.begin(); it != _idleSegment.end(); it++)
	{
		if ((*it).count >= length)
		{
			Segment idleSeg = *it;
			_idleSegment.erase(it);

			seg.first = idleSeg.first;
			seg.count = length;
			if (idleSeg.count > length)
			{
				idleSeg.first = seg.first + length;
				idleSeg.count = idleSeg.count - length;
				_idleSegment.push_back(idleSeg);
			}
			return true;
		}
	}
	return false;
}

template<typename BufferImpl, typename SegmentID>
Segment SegmentBufferManager<BufferImpl, SegmentID>::MoveSegment(const SegmentID& id, size_t newsize)
{
	Segment newSegment;
	if (!FetchIdleSegment(newsize, newSegment))
	{
		newSegment.first = _useSpace;
		newSegment.count = newsize;
		_useSpace += newsize;
	}

	auto it = _usingSegment.find(id);
	if (it != _usingSegment.end())
	{
		auto& oriSegment = it->second;
		_buffer->Memcpy(newSegment.first, oriSegment.first, oriSegment.count);
		_idleSegment.push_back(it->second);
	}

	_usingSegment[id] = newSegment;
	return newSegment;
}

template<typename BufferImpl, typename SegmentID>
void SegmentBufferManager<BufferImpl, SegmentID>::SetAlign(uint64_t align)
{
	_align = std::max(1ull, align);
}

template<typename BufferImpl, typename SegmentID>
const BufferImpl* SegmentBufferManager<BufferImpl, SegmentID>::GetBuffer() const
{
	return _buffer.get();
}

template<typename BufferImpl, typename SegmentID>
uint64_t SegmentBufferManager<BufferImpl, SegmentID>::GetAlign() const
{
	return _align;
}

template<typename BufferImpl, typename SegmentID>
uint64_t SegmentBufferManager<BufferImpl, SegmentID>::GetUseSpace() const
{
	return _useSpace;
}

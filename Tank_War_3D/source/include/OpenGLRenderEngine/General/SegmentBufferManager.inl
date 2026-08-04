#include <algorithm>
#include <execution>

template<typename SegmentID>
SegmentBufferManager<SegmentID>::SegmentBufferManager(size_t initsize)
{
	_buffer.ReSize(initsize);
	_useSpace = 0;
}

template<typename SegmentID>
bool SegmentBufferManager<SegmentID>::AddSegment(const SegmentID& id, void* userData, const char* mem, size_t length, SegmentBufferManager<SegmentID>::SegmentData& data)
{
	auto it = _usingSegment.find(id);
	if (it != _usingSegment.end())
		return false;

	Segment seg;
	if (!FetchIdleSegment(length, seg))
	{
		seg.first = _useSpace;
		seg.count = length;
		_useSpace += length;
	}
	_usingSegment[id] = seg;
	_userDatas[id] = userData;
	WriteData(mem, seg.first, seg.count);

	data.userData = userData;
	data.first = seg.first;
	data.count = seg.count;
	return true;
}

template<typename SegmentID>
SegmentBufferManager<SegmentID>::SegmentData SegmentBufferManager<SegmentID>::SetSegment(const SegmentID& id, void* userData, const char* mem, size_t length)
{
	auto it = _usingSegment.find(id);
	if (it != _usingSegment.end())
	{
		auto oriSegment = it->second;
		if (oriSegment.count >= length)
		{
			WriteData(mem, oriSegment.first, length);
			return SegmentBufferManager<SegmentID>::SegmentData{ userData, oriSegment.first, oriSegment.count };;
		}
		else
		{	//空间不足，移动
			size_t newsize = length;
			auto newSegment = MoveSegment(id, newsize);
			WriteData(mem, newSegment.first, length);
			return SegmentBufferManager<SegmentID>::SegmentData{ userData, newSegment.first, newSegment.count };
		}
	}
	else
	{
		SegmentBufferManager::Segment seg;
		if (!FetchIdleSegment(length, seg))
		{
			seg.first = _useSpace;
			seg.count = length;
			_useSpace += length;
		}
		_userDatas[id] = userData;
		_usingSegment[id] = seg;
		WriteData(mem, seg.first, length);

		return SegmentBufferManager<SegmentID>::SegmentData{ userData, seg.first, seg.count };
	}
}

template<typename SegmentID>
bool SegmentBufferManager<SegmentID>::FindSegment(const SegmentID& id, SegmentBufferManager<SegmentID>::SegmentData& data)
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
		if (it == _userDatas.end())
			data.userData = nullptr;
		data.userData = it->second;
	}

	return true;
}

template<typename SegmentID>
bool SegmentBufferManager<SegmentID>::RemoveSegment(const SegmentID& id, SegmentBufferManager<SegmentID>::SegmentData& data)
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

template<typename SegmentID>
const void* SegmentBufferManager<SegmentID>::GetData()
{
	return _buffer.Data();
}

template<typename SegmentID>
void SegmentBufferManager<SegmentID>::SortAndMegerIdleSegment()
{
	std::sort(std::execution::par_unseq, _idleSegment.begin(), _idleSegment.end(),
		[](const Segment& v1, const Segment& v2)
		{return v1.first < v2.first; }
	);

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

template<typename SegmentID>
bool SegmentBufferManager<SegmentID>::FetchIdleSegment(size_t length, SegmentBufferManager<SegmentID>::Segment& seg)
{
	for (auto it = _idleSegment.begin(); it != _idleSegment.end(); it++)
	{
		if ((*it).count >= length)
		{
			SegmentBufferManager::Segment idleSeg = *it;
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

template<typename SegmentID>
SegmentBufferManager<SegmentID>::Segment SegmentBufferManager<SegmentID>::MoveSegment(const SegmentID& id, size_t newsize)
{
	SegmentBufferManager::Segment newSegment;
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
		memcpy((void*)(_buffer.Byte() + newSegment.first), (void*)(_buffer.Byte() + oriSegment.first), oriSegment.count);
		_idleSegment.push_back(it->second);
	}

	_usingSegment[id] = newSegment;
	return newSegment;
}

template<typename SegmentID>
void SegmentBufferManager<SegmentID>::WriteData(const char* mem, size_t first, size_t length)
{
	if (_buffer.Length() < first + length)
		_buffer.ReSize(std::max(size_t(_buffer.Length() * 1.5), first + length));
	_buffer.Seek(first);
	_buffer.Write(mem, length);
}

template<typename SegmentID>
uint32_t SegmentBufferManager<SegmentID>::GetUseSpace()
{
	return _useSpace;
}

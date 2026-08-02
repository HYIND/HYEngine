#pragma once

#include "Helper/Buffer.h"
#include <unordered_map>

// 持有一段逻辑上分段，但是连续存储的内存
// 可动态查询每一分段的数据信息
// 

//using SegmentID = std::string;

template<typename SegmentID>
class SegmentBufferManager
{
public:
	struct SegmentData
	{
		void* userData;
		size_t first;
		size_t count;
	};

public:
	struct Segment
	{
		size_t first;
		size_t count;
	};

public:
	SegmentBufferManager(size_t initsize = 16);

	bool AddSegment(const SegmentID& id, void* userData, const char* mem, size_t length, SegmentData& data);	// 检查id，重复时添加失败
	SegmentData SetSegment(const SegmentID& id, void* userData, const char* mem, size_t length);				// 不检查id重复，重复则覆盖原有数据
	bool FindSegment(const SegmentID& id, SegmentData& data);													// 查找段
	bool RemoveSegment(const SegmentID& id, SegmentData& data);													// 移除段

	const void* GetData();
	uint32_t GetUseSpace();

private:
	void SortAndMegerIdleSegment();
	bool FetchIdleSegment(size_t length, Segment& seg);
	Segment MoveSegment(const SegmentID& id, size_t newsize);
	void WriteData(const char* mem, size_t first, size_t length);

private:
	Buffer _buffer;

	uint32_t _useSpace;
	std::unordered_map<SegmentID, Segment> _usingSegment;
	std::vector<Segment> _idleSegment;

	std::unordered_map<SegmentID, void*> _userDatas;
};

#include "SegmentBufferManager.inl"

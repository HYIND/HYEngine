#pragma once

#include "SegmentBufferBase.h"
#include <unordered_map>

// 持有一段逻辑上分段，但是连续存储的内存
// 可动态查询每一分段的数据信息
// 

//using SegmentID = std::string;

struct SegmentData
{
	void* userData = nullptr;
	size_t first = 0;
	size_t count = 0;
};
struct Segment
{
	size_t first = 0;
	size_t count = 0;
};

template<typename BufferImpl, typename SegmentID>
class SegmentBufferManager
{

public:
	SegmentBufferManager(uint64_t initsize = 1024);
	SegmentBufferManager(std::unique_ptr<BufferImpl>&& buffer);

	bool AddSegment(const SegmentID& id, void* userData, const void* mem, size_t length, SegmentData& data);	// 检查id，重复时添加失败
	SegmentData SetSegment(const SegmentID& id, void* userData, const void* mem, size_t length);				// 不检查id重复，重复则覆盖原有数据
	bool FindSegment(const SegmentID& id, SegmentData& data);													// 查找并获取段
	bool IsSegmentExist(const SegmentID& id);																	// 仅检查是否存在
	bool RemoveSegment(const SegmentID& id, SegmentData& data);													// 移除段
	bool RemoveSegment(const SegmentID& id);																	// 移除段

	const BufferImpl* GetBuffer();
	uint64_t GetUseSpace();

private:
	void SortAndMegerIdleSegment();
	bool FetchIdleSegment(size_t length, Segment& seg);
	Segment MoveSegment(const SegmentID& id, size_t newsize);

private:
	std::unique_ptr<BufferImpl> _buffer;

	uint64_t _useSpace;
	std::unordered_map<SegmentID, Segment> _usingSegment;
	std::vector<Segment> _idleSegment;

	std::unordered_map<SegmentID, void*> _userDatas;
};

#include "SegmentBufferManager.inl"

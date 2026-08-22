#pragma once

#include <cstdint>

class SegmentBufferBase
{
public:
	SegmentBufferBase() = default;
	virtual ~SegmentBufferBase() = default;

public:
	virtual void ReSize(uint64_t length) = 0;
	virtual void WriteData(const void* mem, uint64_t first, uint64_t length) = 0;
	virtual void Memcpy(uint64_t destFirst, uint64_t originFirst, uint64_t length) = 0;
};

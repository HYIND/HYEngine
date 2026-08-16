#pragma once

#include "SegmentBufferBase.h"

class MemorySegmentBuffer :public SegmentBufferBase
{
public:
	MemorySegmentBuffer();;
	~MemorySegmentBuffer();

	virtual void ReSize(uint64_t length);
	virtual void WriteData(const void* mem, uint64_t first, uint64_t length);
	virtual void Memcpy(uint64_t destFirst, uint64_t originFirst, uint64_t length);
	const void* GetData() const;

private:
	Buffer _buffer;
};
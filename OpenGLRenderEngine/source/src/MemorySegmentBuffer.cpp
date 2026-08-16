#include "OpenGLRenderEngine/General/MemorySegmentBuffer.h"

MemorySegmentBuffer::MemorySegmentBuffer() {}

MemorySegmentBuffer::~MemorySegmentBuffer()
{
	_buffer.Release();
}

void MemorySegmentBuffer::ReSize(uint64_t length) {
	_buffer.ReSize(length);
}

void MemorySegmentBuffer::WriteData(const void* mem, uint64_t first, uint64_t length)
{
	if (_buffer.Length() < first + length)
		_buffer.ReSize(std::max(size_t(_buffer.Length() * 1.5), first + length));
	_buffer.Seek(first);
	_buffer.Write(mem, length);
}

void MemorySegmentBuffer::Memcpy(uint64_t destFirst, uint64_t originFirst, uint64_t length)
{
	memcpy((void*)(_buffer.Byte() + destFirst), (void*)(_buffer.Byte() + originFirst), length);
}

const void* MemorySegmentBuffer::GetData() const {
	return _buffer.Data();
}

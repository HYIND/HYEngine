#include "vkstdafx.h"
#include "VulkanRenderEngine/General/GeneralSegmentBuffer.h"
#include "VulkanRenderEngine/VKContext.h"

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
		_buffer.ReSize(std::max(size_t(_buffer.Length() * expendFactor), first + length));
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

VertexBufferSegmentBuffer::VertexBufferSegmentBuffer()
{
	_block = std::make_shared<VertexBufferBlock>();
}

VertexBufferSegmentBuffer::~VertexBufferSegmentBuffer()
{}

void VertexBufferSegmentBuffer::ReSize(uint64_t length) {
	_block->SetSize(length);
}

void VertexBufferSegmentBuffer::WriteData(const void* mem, uint64_t first, uint64_t length)
{
	uint64_t size = _block->GetSize();
	uint64_t needSize = length + first;
	if (needSize > size)
	{
		uint64_t newSize = std::max(uint64_t(size * expendFactor), needSize + 1);
		ReSize(newSize);
	}
	_block->WriteData(mem, length, first);
}

void VertexBufferSegmentBuffer::Memcpy(uint64_t destFirst, uint64_t srcFirst, uint64_t length) {
	_block->CopySelfData(destFirst, srcFirst, length);
}

std::shared_ptr<VertexBufferBlock> VertexBufferSegmentBuffer::GetBlock() const {
	return _block;
}

IndexBufferSegmentBuffer::IndexBufferSegmentBuffer()
{
	_block = std::make_shared<IndexBufferBlock>();
}

IndexBufferSegmentBuffer::~IndexBufferSegmentBuffer()
{}

void IndexBufferSegmentBuffer::ReSize(uint64_t length) {
	_block->SetSize(length);
}

void IndexBufferSegmentBuffer::WriteData(const void* mem, uint64_t first, uint64_t length)
{
	uint64_t size = _block->GetSize();
	uint64_t needSize = length + first;
	if (needSize > size)
	{
		uint64_t newSize = std::max(uint64_t(size * expendFactor), needSize + 1);
		ReSize(newSize);
	}
	_block->WriteData(mem, length, first);
}

void IndexBufferSegmentBuffer::Memcpy(uint64_t destFirst, uint64_t srcFirst, uint64_t length) {
	_block->CopySelfData(destFirst, srcFirst, length);
}

std::shared_ptr<IndexBufferBlock> IndexBufferSegmentBuffer::GetBlock() const {
	return _block;
}

StorageSegmentBuffer::StorageSegmentBuffer() {
	_block = std::make_shared<StorageBlock>();
}

StorageSegmentBuffer::~StorageSegmentBuffer() {}

void StorageSegmentBuffer::ReSize(uint64_t length) {
	_block->SetSize(length);
}

void StorageSegmentBuffer::WriteData(const void* mem, uint64_t first, uint64_t length)
{
	uint64_t size = _block->GetSize();
	uint64_t needSize = length + first;
	if (needSize > size)
	{
		uint64_t newSize = std::max(uint64_t(size * expendFactor), needSize + 1);
		ReSize(newSize);
	}
	_block->WriteData(mem, length, first);
}

void StorageSegmentBuffer::Memcpy(uint64_t destFirst, uint64_t srcFirst, uint64_t length) {
	_block->CopySelfData(destFirst, srcFirst, length);
}

std::shared_ptr<StorageBlock> StorageSegmentBuffer::GetBlock() const {
	return _block;
}
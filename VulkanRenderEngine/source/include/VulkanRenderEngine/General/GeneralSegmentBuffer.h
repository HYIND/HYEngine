#pragma once

#include "SegmentBufferBase.h"

#include "Helper/Buffer.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"

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
	float expendFactor = 1.5f;		//扩容系数、1.0表示扩容时按需分配，不额外扩容
};

class VertexBufferSegmentBuffer :public SegmentBufferBase
{
public:
	VertexBufferSegmentBuffer();
	~VertexBufferSegmentBuffer();
public:
	virtual void ReSize(uint64_t length);
	virtual void WriteData(const void* mem, uint64_t first, uint64_t length);
	virtual void Memcpy(uint64_t destFirst, uint64_t srcFirst, uint64_t length);
	std::shared_ptr<VertexBufferBlock> GetBlock() const;

private:
	std::shared_ptr<VertexBufferBlock> _block;
	float expendFactor = 1.5f;		//扩容系数、1.0表示扩容时按需分配，不额外扩容
};

class IndexBufferSegmentBuffer :public SegmentBufferBase
{
public:
	IndexBufferSegmentBuffer();
	~IndexBufferSegmentBuffer();
public:
	virtual void ReSize(uint64_t length);
	virtual void WriteData(const void* mem, uint64_t first, uint64_t length);
	virtual void Memcpy(uint64_t destFirst, uint64_t srcFirst, uint64_t length);
	std::shared_ptr<IndexBufferBlock> GetBlock() const;

private:
	std::shared_ptr<IndexBufferBlock> _block;
	float expendFactor = 1.5f;		//扩容系数、1.0表示扩容时按需分配，不额外扩容
};

class StorageSegmentBuffer :public SegmentBufferBase
{
public:
	StorageSegmentBuffer();
	~StorageSegmentBuffer();
public:
	virtual void ReSize(uint64_t length);
	virtual void WriteData(const void* mem, uint64_t first, uint64_t length);
	virtual void Memcpy(uint64_t destFirst, uint64_t srcFirst, uint64_t length);
	std::shared_ptr<StorageBlock> GetBlock() const;

private:
	std::shared_ptr<StorageBlock> _block;
	float expendFactor = 1.5f;		//扩容系数、1.0表示扩容时按需分配，不额外扩容
};
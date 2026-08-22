#pragma once

#define GLEW_STATIC    
#include "GL\glew.h"

#include "SegmentBufferBase.h"

#include "Helper/Buffer.h"
#include "OpenGLRenderEngine/Base/SSBO.h"

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

class GPUBufferSegmentBuffer :public SegmentBufferBase
{
public:
	GPUBufferSegmentBuffer();
	~GPUBufferSegmentBuffer();
public:
	virtual void ReSize(uint64_t length);
	virtual void WriteData(const void* mem, uint64_t first, uint64_t length);
	virtual void Memcpy(uint64_t destFirst, uint64_t srcFirst, uint64_t length);
	GLuint GetID() const;
private:
	void Need();

private:
	unsigned int _buffer;
	uint64_t _size;
};

class SSBOSegmentBuffer :public SegmentBufferBase
{
public:
	SSBOSegmentBuffer();
	~SSBOSegmentBuffer();
public:
	virtual void ReSize(uint64_t length);
	virtual void WriteData(const void* mem, uint64_t first, uint64_t length);
	virtual void Memcpy(uint64_t destFirst, uint64_t srcFirst, uint64_t length);
	std::shared_ptr<SSBO> GetSSBO() const;
private:
	void Need();

private:
	std::shared_ptr<SSBO> _ssbo;
};
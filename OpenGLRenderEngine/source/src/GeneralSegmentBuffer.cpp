#include "OpenGLRenderEngine/General/GeneralSegmentBuffer.h"
#include "OpenGLRenderEngine/General/OpenGLRenderContext.h"

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

GPUBufferSegmentBuffer::GPUBufferSegmentBuffer()
	:_buffer(0), _size(0)
{
}

GPUBufferSegmentBuffer::~GPUBufferSegmentBuffer()
{
	if (_buffer > 0)
	{
		auto guard = THREADCONTEXT->GetBindGuard();
		glDeleteBuffers(1, &_buffer);
	}
}

void GPUBufferSegmentBuffer::ReSize(uint64_t length)
{
	Need();

	auto& newsize = length;
	if (newsize > _size)
	{
		auto guard = THREADCONTEXT->GetBindGuard();

		if (_size > 0)
		{
			GLuint tempBuffer;
			glCreateBuffers(1, &tempBuffer);

			glNamedBufferStorage(tempBuffer, _size, nullptr, 0);
			glCopyNamedBufferSubData(_buffer, tempBuffer, 0, 0, _size);

			glNamedBufferData(_buffer, newsize, nullptr, GL_STATIC_DRAW);
			glCopyNamedBufferSubData(tempBuffer, _buffer, 0, 0, _size);

			glDeleteBuffers(1, &tempBuffer);
		}
		else
			glNamedBufferData(_buffer, newsize, nullptr, GL_STATIC_DRAW);

		_size = newsize;
		//std::cout << std::format("VBOSegmentBuffer Resize {}MB\n", newsize / (1024 * 1024));
	}
}

void GPUBufferSegmentBuffer::WriteData(const void* mem, uint64_t first, uint64_t length)
{
	auto guard = THREADCONTEXT->GetBindGuard();

	Need();

	uint64_t needSize = length + first;
	if (needSize > _size)
	{
		uint64_t newSize = std::max(uint64_t(_size * 1.5), needSize + 1);
		ReSize(newSize);
	}

	glNamedBufferSubData(_buffer, first, length, mem);
}

void GPUBufferSegmentBuffer::Memcpy(uint64_t destFirst, uint64_t srcFirst, uint64_t length)
{
	if (length == 0 || destFirst == srcFirst)
		return;

	if (destFirst + length > _size || srcFirst + length > _size)
		return;

	auto guard = THREADCONTEXT->GetBindGuard();

	Need();

	// 创建临时缓冲
	GLuint tempBuffer;
	glCreateBuffers(1, &tempBuffer);

	// 从原SSBO拷贝到临时缓冲
	glNamedBufferStorage(tempBuffer, length, nullptr, 0);
	glCopyNamedBufferSubData(_buffer, tempBuffer, srcFirst, 0, length);

	// 从临时缓冲拷回原SSBO的目标位置
	glCopyNamedBufferSubData(tempBuffer, _buffer, 0, destFirst, length);

	// 清理
	glDeleteBuffers(1, &tempBuffer);
}

GLuint GPUBufferSegmentBuffer::GetID() const
{
	return _buffer;
}

void GPUBufferSegmentBuffer::Need()
{
	if (_buffer == 0)
	{
		auto guard = THREADCONTEXT->GetBindGuard();
		glCreateBuffers(1, &_buffer);
	}
}

SSBOSegmentBuffer::SSBOSegmentBuffer() {
	_ssbo = std::make_shared<SSBO>();
}

SSBOSegmentBuffer::~SSBOSegmentBuffer() {}

void SSBOSegmentBuffer::ReSize(uint64_t length) {
	_ssbo->SetSize(length);
}

void SSBOSegmentBuffer::WriteData(const void* mem, uint64_t first, uint64_t length)
{
	uint64_t size = _ssbo->GetSize();
	uint64_t needSize = length + first;
	if (needSize > size)
	{
		uint64_t newSize = std::max(uint64_t(size * 1.5), needSize + 1);
		ReSize(newSize);
	}
	_ssbo->WriteData(mem, length, first);
}

void SSBOSegmentBuffer::Memcpy(uint64_t destFirst, uint64_t srcFirst, uint64_t length) {
	_ssbo->CopyData(destFirst, srcFirst, length);
}

std::shared_ptr<SSBO> SSBOSegmentBuffer::GetSSBO() const {
	return _ssbo;
}
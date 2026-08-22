#include "OpenGLRenderEngine/Base/SSBO.h"
#include "OpenGLRenderEngine/General/OpenGLRenderContext.h"

SSBO::SSBO(uint64_t size)
	:_ssboId(0), _size(0)
{
	size = std::max((uint64_t)16, size);
	SetSize(size);
}

SSBO::~SSBO()
{
	if (_ssboId > 0)
	{
		auto guard = THREADCONTEXT->GetBindGuard();
		glDeleteBuffers(1, &_ssboId);
	}
}

void SSBO::SetSize(uint64_t newsize)
{
	if (newsize > _size)
	{
		auto guard = THREADCONTEXT->GetBindGuard();
		Need();

		if (_size > 0)
		{
			GLuint tempBuffer;
			glCreateBuffers(1, &tempBuffer);

			glNamedBufferStorage(tempBuffer, _size, nullptr, 0);
			glCopyNamedBufferSubData(_ssboId, tempBuffer, 0, 0, _size);

			glNamedBufferData(_ssboId, newsize, nullptr, GL_DYNAMIC_DRAW);
			glCopyNamedBufferSubData(tempBuffer, _ssboId, 0, 0, _size);

			glDeleteBuffers(1, &tempBuffer);
		}
		else
			glNamedBufferData(_ssboId, newsize, nullptr, GL_DYNAMIC_DRAW);
		_size = newsize;
	}
}

void SSBO::WriteData(const void* data, uint64_t size, uint64_t offset)
{
	auto guard = THREADCONTEXT->GetBindGuard();

	Need();

	if (size + offset > _size)
		SetSize(size + offset);

	glNamedBufferSubData(_ssboId, offset, size, data);
}

void SSBO::CopyData(uint64_t destFirst, uint64_t srcFirst, uint64_t length)
{
	if (length == 0 || destFirst == srcFirst)
		return;

	if (destFirst + length > _size || srcFirst + length > _size)
		return;

	Need();

	// 创建临时缓冲
	GLuint tempBuffer;
	glCreateBuffers(1, &tempBuffer);

	// 从原SSBO拷贝到临时缓冲
	glNamedBufferStorage(tempBuffer, length, nullptr, 0);
	glCopyNamedBufferSubData(_ssboId, tempBuffer, srcFirst, 0, length);

	// 从临时缓冲拷回原SSBO的目标位置
	glCopyNamedBufferSubData(tempBuffer, _ssboId, 0, destFirst, length);

	// 清理
	glDeleteBuffers(1, &tempBuffer);
}


void SSBO::Need()
{
	if (_ssboId == 0)
		glCreateBuffers(1, &_ssboId);
}

GLuint SSBO::GetID() { return _ssboId; }

uint64_t SSBO::GetSize() { return _size; }


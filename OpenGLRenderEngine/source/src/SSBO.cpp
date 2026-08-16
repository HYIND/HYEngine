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
	auto guard = THREADCONTEXT->GetBindGuard();
	if (_ssboId > 0)
		glDeleteBuffers(1, &_ssboId);
}

void SSBO::SetSize(uint64_t newsize)
{
	if (newsize > _size)
	{
		auto guard = THREADCONTEXT->GetBindGuard();
		Need();

		if (_size > 0)
		{
			//std::vector<unsigned char> oldData(_size);
			//glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, _size, oldData.data());
			//glBufferData(GL_SHADER_STORAGE_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
			//glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, oldData.size(), oldData.data());

			GLuint tempBuffer;
			glGenBuffers(1, &tempBuffer);

			glBindBuffer(GL_COPY_READ_BUFFER, _ssboId);
			glBindBuffer(GL_COPY_WRITE_BUFFER, tempBuffer);

			glBufferData(GL_COPY_WRITE_BUFFER, _size, nullptr, GL_STATIC_DRAW);

			glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, _size);

			glBufferData(GL_SHADER_STORAGE_BUFFER, newsize, nullptr, GL_DYNAMIC_DRAW);

			glBindBuffer(GL_COPY_READ_BUFFER, tempBuffer);
			glBindBuffer(GL_COPY_WRITE_BUFFER, _ssboId);
			glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, _size);

			glDeleteBuffers(1, &tempBuffer);
		}
		else
			glBufferData(GL_SHADER_STORAGE_BUFFER, newsize, nullptr, GL_DYNAMIC_DRAW);
		_size = newsize;
	}
}

void SSBO::WriteData(const void* data, uint64_t size, uint64_t offset)
{
	auto guard = THREADCONTEXT->GetBindGuard();

	Need();

	if (size + offset > _size)
		SetSize(size + offset);

	glBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, size, data);
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
	glGenBuffers(1, &tempBuffer);

	// 从原VBO拷贝到临时缓冲
	glBindBuffer(GL_COPY_READ_BUFFER, _ssboId);
	glBindBuffer(GL_COPY_WRITE_BUFFER, tempBuffer);
	glBufferData(GL_COPY_WRITE_BUFFER, length, nullptr, GL_STATIC_DRAW);
	glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, srcFirst, 0, length);

	// 从临时缓冲拷回原VBO的目标位置
	glBindBuffer(GL_COPY_READ_BUFFER, tempBuffer);
	glBindBuffer(GL_COPY_WRITE_BUFFER, _ssboId);
	glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, destFirst, length);

	// 清理
	glDeleteBuffers(1, &tempBuffer);
}


void SSBO::Need()
{
	if (_ssboId == 0)
		glGenBuffers(1, &_ssboId);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, _ssboId);
}

GLuint SSBO::GetID() { return _ssboId; }

uint64_t SSBO::GetSize() { return _size; }


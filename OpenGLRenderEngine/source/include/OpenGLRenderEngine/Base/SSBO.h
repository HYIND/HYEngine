#pragma once

#define GLEW_STATIC
#include "GL\glew.h"
#include <stdint.h>

//SSBO管理
class SSBO
{
public:
	SSBO(uint64_t size = 0);
	~SSBO();

	void SetSize(uint64_t size);
	void WriteData(const void* data, uint64_t size, uint64_t offset = 0);
	void CopyData(uint64_t destFirst, uint64_t srcFirst, uint64_t length); //ssbo内部数据之间拷贝

	GLuint GetID();
	uint64_t GetSize();

private:
	void Need();

private:
	GLuint _ssboId;
	uint64_t _size;
};


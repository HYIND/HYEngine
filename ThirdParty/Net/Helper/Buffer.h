#pragma once

#include <cstdint>
#include <string.h>
#include <iostream>

#include "NetExportMarco.h"

class NET_API Buffer
{

public:
	Buffer();
	Buffer(const Buffer& other);
	Buffer(Buffer&& other);
	Buffer(const uint64_t length);
	Buffer(const char* source, uint64_t length);
	Buffer(const std::string& source);
	~Buffer();

	Buffer& operator=(const Buffer& other);
	Buffer& operator=(Buffer&& other);

	void* Data();
	const void* Data() const;
	char* Byte();
	const char* Byte() const;

	uint64_t Length() const;
	uint64_t Position() const;
	uint64_t Remain() const;

	void CopyFromBuf(const char* buf, uint64_t length); // 拷贝
	void CopyFromBuf(const Buffer& other);
	void QuoteFromBuf(Buffer& other);  // 以引用的形式占有一段内存,other重置
	void QuoteFromBuf(Buffer&& other); // 以引用的形式占有一段内存,other重置

	/**
	 * 以下读写操作均与pos相关，并引起相关流pos变化
	 */

	 /** 以下操作引起当前流pos变化*/
	uint64_t Write(const Buffer& other);                    // 从pos开始，向当前流写入数据，数据来源为其他流
	uint64_t Write(const std::string& str);                 // 从pos开始，向当前流写入数据
	uint64_t Write(const void* buf, const uint64_t length); // 从pos开始，向当前流写入数据
	uint64_t Read(void* buf, const uint64_t length);        // 从pos开始，读出当前流内的数据,返回实际读取字节数
	uint64_t Seek(const uint64_t index);                    // 设置pos

	/** 以下操作引起其他流pos变化*/
	uint64_t Append(Buffer& other);                  // 从其他流的pos开始读取剩余内容，向当前流末尾追加数据，返回实际追加的字节数，该操作会引起其他流pos变化
	uint64_t Append(Buffer& other, uint64_t length); // 从其他流的pos开始读取length字节，向当前流末尾追加数据，返回实际追加的字节数，该操作会引起其他流pos变化

	/** 以下操作引起当前流与其他流pos变化*/
	uint64_t WriteFromOtherBufferPos(Buffer& other);                  // 从当前流pos开始，从其他流的pos开始读取剩余内容，向当前流写入，返回实际读取的字节数，该操作会引起其他流的pos变化
	uint64_t WriteFromOtherBufferPos(Buffer& other, uint64_t length); // 从当前流pos开始，从其他流的pos开始读取length字节，向当前流写入，返回实际读取的字节数，该操作会引起其他流的pos变化

	void Release();                                       // 释放当前流，并重置pos,length
	void ReSize(const uint64_t length);                   // 重置流大小，可能会截断原流
	void Shift(const uint64_t length);                    // 从流的头部开始，移除该流的前n个字节
	void Unshift(const void* buf, const uint64_t length); // 在该流的头部添加n个字节

private:
	char* _buf = nullptr;
	uint64_t _length = 0;
	uint64_t _pos = 0;
};

#define SAFE_DELETE(x) \
    if (x)             \
    {                  \
        delete x;      \
        x = nullptr;   \
    }

#define SAFE_DELETE_ARRAY(x) \
    if (x)                   \
    {                        \
        delete[] x;          \
        x = nullptr;         \
    }

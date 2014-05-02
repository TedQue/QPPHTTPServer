/* Copyright (C) 2012 阙荣文
 *
 * 这是一个开源免费软件,您可以自由的修改和发布.
 * 禁止用作商业用途.
 *
 * 联系原作者: querw@sina.com 
*/

#pragma once

#ifndef _MEMFILE_HEADER_PROTECT_
#define _MEMFILE_HEADER_PROTECT_

/*
memfile.h

内存缓冲,可以读写.
模仿标准文件接口
区别在于读写分别有独立的指针, read() 和 write() 交叉执行时,不需要使用 seek() 来移动.

by 阙荣文 - Que's C++ Studio
2010-12-16

*/

class memfile
{
private:
	char *_buffer;
	size_t _bufLen;

	size_t _readPos;
	size_t _writePos;
	size_t _fileSize;

	size_t _maxSize;
	size_t _memInc;
	bool _selfAlloc;

	size_t space();
	size_t reserve(size_t s);

	memfile(const memfile &other);
	memfile& operator = (const memfile &other);

public:
	memfile(size_t memInc = 1024, size_t maxSize = SIZE_T_MAX);
	memfile(const void* buf, size_t len);
	~memfile();

	size_t write(const void *buf, size_t len); // 返回写入的字节数
	size_t puts(const char* buf); // 返回写入的字节数,不含结束符0
	size_t putc(const char ch);
	size_t seekp(long offset, int origin); // 返回0 表示成功.
	size_t tellp() const;

	size_t read(void *buf, size_t size); // 返回读取字节数
	char getc();
	size_t gets(char *buf, size_t size); // 返回读取的字节数,包含换行符
	size_t seekg(long offset, int origin);
	size_t tellg() const;
	
	void* buffer();
	inline size_t bufferSize()  const { return _bufLen; }

	inline size_t fsize() const { return _fileSize; }
	void trunc(bool freeBuf = true);
	bool eof() const;
	
	bool reserve(size_t r, void **buf, size_t *len);
};

#endif
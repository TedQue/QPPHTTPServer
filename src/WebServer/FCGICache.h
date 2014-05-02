/* Copyright (C) 2011 阙荣文
 *
 * 这是一个开源免费软件,您可以自由的修改和发布.
 * 禁止用作商业用途.
 *
 * 联系原作者: querw@sina.com 
*/

#pragma once
#include "WINFile.h"

/*
* 作为 FCGIResponder 的缓存
* 一个线程从 FCGI 服务器接收到数据后写入,另一个线程从缓存中读取.
* 内部维护一块固定长度的内存,如果数据长度超出,则使用文件系统.
*
*/

class FCGICache : public INoCopy
{
private:
	size_t _size;

	/*
	* 文件缓冲区
	*/
	WINFile *_file;
	std::string _fileName;
	long _frpos;
	long _fwpos;

	/*
	* 内存缓冲区
	* 自动增长直到最大值,然后再写文件.
	*/
	byte *_buf;
	size_t _bufSize;
	size_t _rpos;
	size_t _wpos;
	
	size_t fillBuf(); /* 从临时文件中读取数据填充缓冲区 */
public:
	FCGICache(size_t bufSize, const std::string &tmpFileName);
	~FCGICache();

	bool empty();
	size_t size();
	size_t read(void *buf, size_t len);
	size_t write(const void *buf, size_t len);
	size_t puts(const char *str);
};


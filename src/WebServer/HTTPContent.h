/* Copyright (C) 2011 阙荣文
 *
 * 这是一个开源免费软件,您可以自由的修改和发布.
 * 禁止用作商业用途.
 *
 * 联系原作者: querw@sina.com 
*/

#pragma once

/*
* HTTPContent
* 用作包装 HTTP 响应报文中的内容部分,有可能包含以下几种数据类型:
* 1. 一段文本,如 HTTP 500 等错误提示.
* 2. 一个目录文本流,浏览目录时得到的文件列表.
* 3. 一个只读文件的部分或者全部.
*
*/

#include "HTTPDef.h"
#include "memfile.h"

#define OPEN_NONE 0
#define OPEN_FILE 1
#define OPEN_TEXT 2
#define OPEN_BINARY 3
#define OPEN_HTML 4
#define OPEN_DIR 5


class HTTPContent : public INoCopy
{
public:
	HTTPContent();
	virtual ~HTTPContent();

protected:
	int _openType;
	std::string _contentType;

	std::string _fileName;
	WINFile _file;
	struct _stat32i64 _fileInf;

	memfile _memfile;
	
	__int64 _from;
	__int64 _to;

	std::string getContentTypeFromFileName(const char* fileName);
	bool writable();

public:
	bool open(const std::string &fileName, __int64 from = 0, __int64 to = 0); /* 打开一个只读文件 */
	bool open(const std::string &urlStr, const std::string &filePath); /* 打开一个目录 */
	bool open(const char* buf, int len, int type); /* 打开一段 mem buffer */
	void close();

	std::string contentType();
	__int64 contentLength();
	std::string lastModified();
	std::string etag();
	std::string contentRange();
	
	bool isFile();
	bool isOpen();
	bool eof();
	
	size_t read(void* buf, size_t len);
	size_t write(const void* buf, size_t len);
	size_t writeString(const char* str);
};

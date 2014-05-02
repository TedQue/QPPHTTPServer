/* Copyright (C) 2011 阙荣文
 *
 * 这是一个开源免费软件,您可以自由的修改和发布.
 * 禁止用作商业用途.
 *
 * 联系原作者: querw@sina.com 
*/

#pragma once
#include <list>
#include "memfile.h"
#include "fastcgi.h"
/*
* 包装FastCGI Record, FastCGI Record 是 FastCGI 协议的基本通讯单位.
* 一个 Record 的结构如下:
	typedef struct {
		unsigned char version;
		unsigned char type;
		unsigned char requestIdB1;
		unsigned char requestIdB0;
		unsigned char contentLengthB1;
		unsigned char contentLengthB0;
		unsigned char paddingLength;
		unsigned char reserved;

		unsigned char contentData[contentLength];
		unsigned char paddingData[paddingLength];
	} FCGI_Record;
* 前8个字节是 FCGI_Header 结构, 后面的数据由 FCGI_Header 指定的内容长度 contentLength 和 对齐长度 paddingLength 指定.
* 可以认为就是一段变长的内存缓冲.
* 一些特定类型的 Record contentData 有特定的预定义结构,如 FCGI_BeginRequestBody, FCGI_EndRequestBody 和 FCGI_UnknownTypeBody.
*
* class FCGIRecord 的目的是封装FastCGI record以方便编程访问. 包括 reader 和 writer 分别用于解析和生成 FastCGI record.
*/

/* 
* Name - Value pair 
*/
typedef struct 
{ 
	const unsigned char* data; 
	size_t len; 
}nv_t;
typedef std::pair<nv_t, nv_t> nvpair_t;
typedef std::list<nvpair_t> nvlist_t;

class FCGIRecord : public INoCopy
{
private:
	memfile _buffer; /* record 原始数据 */
	
public:
	FCGIRecord();
	~FCGIRecord();

	static size_t toNumber2(const unsigned char* src);
	static size_t toNumber14(const unsigned char* src, size_t *bytes);
	static void toBytes(void* dest, size_t number, size_t bytes);

	size_t write(const void* buf, size_t len); /* 可以分多次写入一个完整的record,返回值小于 len 表述已经接收到一个完整记录 */
	size_t read(void* buf, size_t len); /* 输出为流,可以分段弹出数据直到返回0表示输出完毕. */
	size_t writeHeader(const void *buf, size_t len);
	void reset();
	bool check(); /* 检测是否是一个完整的 record */
	const void* buffer();
	size_t size();
	
	bool setHeader(unsigned short requestId, int type);
	bool setBeginRequestBody(unsigned short role, bool keepConn); /* FCGI_BEGIN_REQUEST */
	bool setEndRequestBody(unsigned int appStatus, unsigned char protocolStatus); /* FCGI_END_REQUEST */
	bool setUnknownTypeBody(); /* FCGI_UNKNOWN_TYPE */
	bool addNameValuePair(nv_t n, nv_t v); /* FCGI_PARAMS,FCGI_GET_VALUES,FCGI_GET_VALUES_RESULT */
	bool addNameValuePair(const char* n, const char* v);
	bool addBodyData(unsigned char* buf, size_t len); /* 通用,FCGI_STDIN,FCGI_STDOUT,FCGI_STDERR,FCGI_DATA*/
	bool setEnd(); /* 打包 */

	unsigned char getType();
	bool getHeader(FCGI_Header &header);
	size_t getContentLength(const FCGI_Header &header);
	bool getBeginRequestBody(unsigned short &role, bool &keepConn); /* FCGI_BEGIN_REQUEST */
	bool getEndRequestBody(unsigned int &appStatus, unsigned char &protocolStatus); /* FCGI_END_REQUEST */
	size_t getNameValuePairCount();
	bool getNameValuePair(int index, nv_t &n, nv_t &v); /* FCGI_PARAMS,FCGI_GET_VALUES,FCGI_GET_VALUES_RESULT */
	const void* getBodyData(); /* 通用,FCGI_STDIN,FCGI_STDOUT,FCGI_STDERR,FCGI_DATA*/
	size_t getBodyLength();
};

class FCGIRecordWriter : public INoCopy
{
private:
	memfile &_buf;
	size_t _headerPos;

	size_t write(const void *buf, size_t len);
public:
	FCGIRecordWriter(memfile &buf);
	~FCGIRecordWriter();

	size_t writeHeader(unsigned short requestId, int type);
	size_t writeBeginRequestBody(unsigned short role, bool keepConn); /* FCGI_BEGIN_REQUEST */
	size_t writeEndRequestBody(unsigned int appStatus, unsigned char protocolStatus); /* FCGI_END_REQUEST */
	size_t writeUnknownTypeBody(unsigned char t); /* FCGI_UNKNOWN_TYPE */
	size_t writeNameValuePair(const char* name, const char* val);
	size_t writeBodyData(const unsigned char* buf, size_t len); /* 通用,FCGI_STDIN,FCGI_STDOUT,FCGI_STDERR,FCGI_DATA*/
	size_t writeEnd();
};

class FCGIRecordReader : public INoCopy
{
private:
	const void* _buffer;
	size_t _len;
	size_t _pos;

	size_t read(void* dest, size_t len);
	size_t putback(size_t len);
	size_t space();
public:
	FCGIRecordReader(const void *buf, size_t len);
	~FCGIRecordReader();

	size_t pos();
	size_t readHeader(unsigned char &t, unsigned short &requestId, unsigned short &contentLength);
	size_t readHeader(FCGI_Header &header);
	size_t readBeginRequestBody(unsigned short &role, bool &keepConn); /* FCGI_BEGIN_REQUEST */
	size_t readEndRequestBody(unsigned int &appStatus, unsigned char &protocolStatus); /* FCGI_END_REQUEST */
	size_t readNameValuePair(const char* &n, size_t &nlen, const char* &val, size_t &vlen); /* FCGI_PARAMS,FCGI_GET_VALUES,FCGI_GET_VALUES_RESULT */
	size_t readBodyData(void* buf, size_t len); /* 通用,FCGI_STDIN,FCGI_STDOUT,FCGI_STDERR,FCGI_DATA*/
};
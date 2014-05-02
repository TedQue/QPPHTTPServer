/* Copyright (C) 2012 阙荣文
 *
 * 这是一个开源免费软件,您可以自由的修改和发布.
 * 禁止用作商业用途.
 *
 * 联系原作者: querw@sina.com 
*/


#pragma once
#include <string>
#include <list>
#include "HTTPDef.h"
#include "memfile.h"

/*
* HTTPResponseHeader 用来生成 HTTP 响应头
*/

class HTTPResponseHeader : public INoCopy
{
private:
	typedef std::list<std::pair<std::string, std::string>> fields_t;
	fields_t _headers;	// 响应头的关联数组
	memfile _buf; // 输出缓冲
	int _resCode; // 响应码

	std::string getFirstLine(); // 根据制定的 Servercode 返回对应的 HTTP响应头的第一行,包括换行符.
	size_t write(const std::string &str);
	fields_t::iterator find(const std::string &name);

public:
	HTTPResponseHeader();
	~HTTPResponseHeader();

	int setResponseCode(int resCode); // 设置响应码,返回旧值
	size_t addDefaultFields(); // 添加默认域
	bool add(const std::string &name, const std::string &val); // 添加域
	bool add(const std::string &fields); // 添加域,以行为单位,一次可添加多个域
	bool remove(const std::string &name); // 删除域
	bool getField(const std::string &name, std::string &val);
	std::string getHeader();

	bool format(); // 格式化缓冲区
	size_t read(byte *buf, size_t len); // 读取缓冲区
	bool eof(); // 读取是否结束
	void reset(); // 重置
};


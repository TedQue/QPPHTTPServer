/* Copyright (C) 2012 阙荣文
 *
 * 这是一个开源免费软件,您可以自由的修改和发布.
 * 禁止用作商业用途.
 *
 * 联系原作者: querw@sina.com 
*/

#pragma once
#include "HTTPDef.h"
#include "XmlDocument.h"

/*
* 实现 IHTTPConfig 作为 HTTPServer 的配置接口
* 用 XML 文件实现.
*/

class HTTPConfig : public INoCopy, public IHTTPConfig
{
private:
	XMLDocument _xmlDoc;
	XMLHANDLE _curFcgiServerXmlHandle;

public:
	HTTPConfig();
	~HTTPConfig();

	bool load(const std::string &fileName);
	bool save(const std::string &fileName);

	// 设置
	bool setDocRoot(const std::string &str);
	bool setTmpRoot(const std::string &str);
	bool setDefaultFileNames(const std::string &str);
	bool setIp(const std::string &str);
	bool setPort(u_short p);
	bool setDirVisible(bool visible);
	bool setMaxConnections(size_t n);
	bool setMaxConnectionsPerIp(size_t n);
	bool setMaxConnectionSpeed(size_t n);
	bool setSessionTimeout(size_t n);
	bool setRecvTimeout(size_t n);
	bool setSendTimeout(size_t n);
	bool setKeepAliveTimeout(size_t n);
	bool addFcgiServer(const fcgi_server_t *serverInf);
	bool removeFcgiServer(const std::string &name);
	bool updateFcgiServer(const std::string &name, const fcgi_server_t *serverInf);

	bool setAutoRun(bool yes);
	bool autoRun();

	bool screenLog();
	bool enableScreenLog(bool enabled);

	std::string logFileName();
	bool setLogFileName(const std::string &fileName);

	slogger::LogLevel logLevel();
	bool setLogLevel(slogger::LogLevel ll);

	//  通用方法
	bool set(const std::string &path, const std::string &v);
	bool get(const std::string &path, std::string &v);

	// IHTTPConfig 实现
	std::string docRoot();
	std::string tmpRoot();
	std::string defaultFileNames();
	std::string ip();
	u_short port();
	bool dirVisible();
	size_t maxConnections();
	size_t maxConnectionsPerIp();
	size_t maxConnectionSpeed();
	size_t sessionTimeout();
	size_t recvTimeout();
	size_t sendTimeout();
	size_t keepAliveTimeout();

	bool getFirstFcgiServer(fcgi_server_t *serverInf);
	bool getNextFcgiServer(fcgi_server_t *serverInf);
};


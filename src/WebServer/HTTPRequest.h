/* Copyright (C) 2011 阙荣文
 *
 * 这是一个开源免费软件,您可以自由的修改和发布.
 * 禁止用作商业用途.
 *
 * 联系原作者: querw@sina.com 
*/

#pragma once

#include "HTTPDef.h"
#include "HTTPContent.h"
#include "memfile.h"
#include "IOCPNetwork.h"

/*
* HTTP 协议中的"请求"报文的封装
*
* HTTPRequest 对象实现了 IRequest 接口,开始运行后从客户端读取整个 HTTP请求报文,包括请求头和 POST 内容
* 
*/

class HTTPRequest : public IRequest, public INoCopy
{
protected:
	HighResolutionTimer _hrt;
	memfile _header;
	memfile *_postData;
	WINFile *_postFile;
	std::string _postFileName;
	bool _isHeaderRecved;
	IHTTPServer *_server;
	IOCPNetwork *_network;
	iocp_key_t _clientSock;
	byte* _sockBuf;
	size_t _sockBufLen;
	conn_id_t _connId;
	size_t _bytesRecv;
	size_t _contentLength;
	__int64 _startTime;

	void deleteSocketBuf();
	void close(int exitCode);
	size_t push(const byte* data, size_t len); // 套接字收到数据后,推入到 HTTP Request 实例中.
	void onRecv(int flags, size_t bytesTransfered);
	static void IOCPCallback(iocp_key_t s, int flags, bool result, int transfered, byte* buf, size_t len, void* param);

public:
	HTTPRequest(IHTTPServer *server, IOCPNetwork *network);
	virtual ~HTTPRequest();

	inline conn_id_t getConnectionId() { return _connId; }
	inline size_t getTotalRecvBytes() { return _bytesRecv; }
	HTTP_METHOD method(); // 返回HTTP 方法
	std::string uri(bool decode); // 返回客户端请求的对象(已经经过UTF8解码,所以返回宽字符串)
	std::string field(const char* key); // 返回请求头中的一个字段(HTTP头中只有ANSI字符,所以返回string).
	bool range(__int64 &from, __int64 &to);
	bool keepAlive();
	size_t contentLength(); /* 请求头中的 Content-Length 字段的值 */
	__int64 startTime() { return _startTime; }

	std::string getHeader();

	bool isValid();
	size_t headerSize();
	size_t size();
	size_t read(byte* buf, size_t len);
	bool eof();
	
	int run(conn_id_t connId, iocp_key_t clientSock, size_t timeout);
	bool stop(int ec);
	bool reset();
};

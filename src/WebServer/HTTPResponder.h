/* Copyright (C) 2012 阙荣文
 *
 * 这是一个开源免费软件,您可以自由的修改和发布.
 * 禁止用作商业用途.
 *
 * 联系原作者: querw@sina.com 
*/

#pragma once
#include "HTTPDef.h"
#include "memfile.h"
#include "HTTPResponseHeader.h"
#include "HTTPRequest.h"

/*
* HTTPResponder 类是接口 IResponder 的实现,封装了 HTTP 协议的响应报文.
* HTTPResponder 接受 IRequest HTTP 请求报文为参数,生成对应的响应报文,并把数据发送到 HTTP 客户端.
* 
*/

class HTTPContent;
class HTTPResponder : public IResponder, public INoCopy
{
protected:
	HTTPResponseHeader _header;
	IRequest *_request;
	IHTTPServer *_server;
	IOCPNetwork *_network;
	iocp_key_t _clientSock;
	conn_id_t _connId;
	__int64 _bytesSent;
	__int64 _bytesRecv;
	int _svrCode;
	HTTPContent* _content;
	byte* _sockBuf;
	size_t _sockBufLen;

	static void IOCPCallback(iocp_key_t s, int flags, bool result, int transfered, byte* buf, size_t len, void* param);
	void onSend(size_t bytesTransfered, int flags);
	
	bool makeResponseHeader(int svrCode);
	bool sendToClient();
	void close(int ct);

public:
	HTTPResponder(IHTTPServer *server, IOCPNetwork *network);
	virtual ~HTTPResponder();

	inline conn_id_t getConnectionId() { return _connId; }
	inline __int64 getTotalSentBytes() { return _bytesSent; }
	inline __int64 getTotalRecvBytes() { return _bytesRecv; }
	inline int getServerCode() { return _svrCode; }
	
	/*
	* 返回响应头
	*/
	std::string getHeader();

	int run(conn_id_t connId, iocp_key_t clientSock, IRequest *request);
	bool stop(int ec);
	bool reset();
};
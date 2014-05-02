/* Copyright (C) 2012 阙荣文
 *
 * 这是一个开源免费软件,您可以自由的修改和发布.
 * 禁止用作商业用途.
 *
 * 联系原作者: querw@sina.com 
*/

#pragma once
#include <list>
#include "HTTPDef.h"
#include "memfile.h"
#include "HTTPResponseHeader.h"
#include "HTTPRequest.h"
#include "FCGIFactory.h"
#include "FCGIRecord.h"
#include "HTTPResponder.h"
#include "FCGICache.h"

/* 
* FCGI 协议的 Responder, FCGIResponder 接收 HTTPRequest 为参数,控制和FCGI服务器间的通信,并把响应流转发到 HTTP 客户端
*
*/

class FCGIResponder : public IResponder, public INoCopy
{
protected:
	/* 
	* Responder 共有 
	*/
	HTTPResponseHeader _header;
	IRequest *_request;
	IHTTPServer *_server;
	IOCPNetwork *_network;
	iocp_key_t _clientSock;
	conn_id_t _connId;
	__int64 _bytesSent;
	__int64 _bytesRecv;
	int _svrCode;

	/* 
	* Fast CGI 专有 
	*/
	typedef struct buffer_t
	{
		byte* buf;	// 缓冲
		size_t len;	// 有效数据长度
		size_t size; // 缓冲大小
	};
	__int64 _bytesFCGISent;
	__int64 _bytesFCGIRecv;
	FCGIFactory *_fcgiFactory;
	fcgi_conn_t *_fcgiConnection;
	bool _cacheAll;
	FCGICache *_cache;
	Lock _lock;
	
	/*
	* 3个数据流
	* 1. HTTPRequest -> FCGI Server
	* 2. FCGI Server -> Cache
	* 3. Cache -> HTTP Client
	*
	* 数据流1是相对独立的,只有在数据流1完成后,才启动数据流2和数据流3.
	* 数据流2,3并发执行.
	* 当数据流3的速度超过数据2的速度时,数据流3可能会暂停,所以需要数据流2不停的启动它.
	*/
	int _exitCode;

	/* 数据流1: HTTPRequest -> FCGI Server */
	memfile *_fcgiSendBuf; /* 数据流1缓冲区,发送使用变长缓冲,因为可能要插入额外的数据 */
	buffer_t *_postDataBuf; /* POST 数据缓冲区*/
	int sendPostData(); /* 读取 HTTPRequest 中的 POST DATA */
	int sendToFCGIServer();

	/* 数据流2: FCGI Server -> Cache */
	int _ds2Status; /* 数据流2的状态: 完毕,出错 */
	buffer_t *_fcgiRecvBuf;
	bool _isFCGIHeaderReceived;
	bool _chunkCoding;
	bool _chunkEndSent;
	FCGIRecord *_stdoutRecord;
	memfile _fcgiResponseHeader; /* 缓存发自FCGI server的HTTP响应头部分 */
	int recvFromFCGIServer();
	bool parseStdout(const byte *buf, size_t len); /* 从FCGI收到数据是调用返回是否收到 FCGI_END_REQUEST */
	size_t writeToCache(const void *data, size_t len); /* 把发送到 HTTP 客户端的数据写入缓存,返回写入的字节数 */

	/* 数据流3: Cache -> HTTP */
	int _ds3Status; /* 数据流3的状态: 完毕,出错 */
	buffer_t *_httpSendBuf;
	int sendToHTTPClient();
	bool hasData(); /* 是否有数据需要发送到 HTTP 客户端 */

	/* 如果 FCGI 服务器错误,并且还没有发送过任何数据到 HTTP 客户端,则发送 HTTP 503/500 */
	memfile* _httpErrorCache;
	int sendHttpError(int errCode, const char *msg);
	

	/*
	* 内部函数,这些函数只是工具函数,返回状态值(HTTP_CONNECTION_EXITCODE),不记录状态,不回调,不加锁.
	*/
	buffer_t* allocBuffer();
	void freeBuffer(buffer_t *buf);

	int initFCGIEnv();

	bool isExitPoint();
	void close(int exitCode);
protected:
	/* 
	* 网络事件回调 
	*/
	static void IOCPCallback(iocp_key_t s, int flags, bool result, int transfered, byte* buf, size_t len, void* param);
	static void onFcgiConnectionReady(fcgi_conn_t *conn, void *param);

	void onConnection(fcgi_conn_t *conn);
	void onHTTPSend(size_t bytesTransfered, int flags);
	void onFCGISend(size_t bytesTransfered, int flags);
	void onFCGIRecv(size_t bytesTransfered, int flags);

public:
	FCGIResponder(IHTTPServer *server, IOCPNetwork *network, FCGIFactory *fcgiFactory);
	~FCGIResponder();

	/* 
	* 访问属性 
	*/
	inline conn_id_t getConnectionId() { return _connId; }
	inline __int64 getTotalSentBytes() { return _bytesSent; }
	inline __int64 getTotalRecvBytes() { return _bytesRecv; }
	inline int getServerCode() { return _svrCode; }

	/*
	* 返回响应头
	*/
	std::string getHeader();

	/* 
	* 控制运行 
	*/
	int run(conn_id_t connId, iocp_key_t clientSock, IRequest *request);
	bool stop(int ec);
	bool reset();
};


/* Copyright (C) 2011 阙荣文
 *
 * 这是一个开源免费软件,您可以自由的修改和发布.
 * 禁止用作商业用途.
 *
 * 联系原作者: querw@sina.com 
*/

#if !defined(_HTTPFILESERVER_H_)
#define _HTTPFILESERVER_H_

/*
#include "winsock2.h"
#pragma comment(lib, "ws2_32.lib")
*/

#include "HTTPDef.h"
#include "FCGIFactory.h"

/*
* HTTPServer类
* 目的: 代表一个HTTP Server在内存中的存在.
* 1. 管理侦听套接字.
* 2. 管理HTTP连接包括 HTTPRequest, HTTPResponder 等,当有新连接进入时创建 HTTPRequest 对象并调用它的入口函数 run()
*	 以启动对这个新连接的处理过程.
* 3. 提供参数获取的渠道.
* 
* by 阙荣文 - Que's C++ Studio
* 2011.7.7
*/

class HTTPServer : public IHTTPServer, public INoCopy
{
protected:
	/*
	* 侦听套接字网络事件
	*/
	typedef struct
	{
		byte* buf;
		size_t len;
	}accept_context_t;

	/*
	* 连接定义
	*/
	typedef struct			
	{
		iocp_key_t		clientSock;				// 客户端套接字
		IRequest*		request;				// HTTP请求描述符
		IResponder*		responder;				// HTTP响应描述符

		__int64			startTime;				// 连接开始时的时间
		char			ip[MAX_IP_LENGTH + 1];	// 客户端连接IP地址
		unsigned int	port;					// 客户端连接端口
	}connection_context_t;
	typedef std::map<iocp_key_t, connection_context_t*> connection_map_t;

	/*
	* 环境配置
	*/
	std::string _docRoot; /*根目录*/
	std::string _tmpRoot; /* 临时文件跟目录 */
	bool _isDirectoryVisible; /*是否允许浏览目录*/
	std::string _dftFileName; /*默认文件名*/
	std::string _ip; /*服务器IP地址*/
	u_short _port; /*服务器侦听端口*/
	size_t _maxConnections; /*最大连接数*/
	size_t _maxConnectionsPerIp; /*每个IP的最大连接数*/
	size_t _maxConnectionSpeed; /*每个连接的速度限制,单位 b/s.*/

	unsigned long _sessionTimeout; /*会话超时*/
	unsigned long _recvTimeout; /*recv, connect, accept 操作的超时*/
	unsigned long _sendTimeout; /*send 操作的超时*/
	unsigned long _keepAliveTimeout; /* keep-alive 超时 */

	/*
	* 内部变量
	*/
	bool _isRuning;
	HighResolutionTimer _hrt;
	FCGIFactory *_fcgiFactory;
	IOCPNetwork _network;
	iocp_key_t _sListen;
	SOCKET _sockNewClient;
	accept_context_t _acceptContext;
	connection_map_t _connections; /* 客户信息列表,每个连接(客户)对应一个记录(connection_context_t*)指针 */
	str_int_map_t _connectionIps; /* 客户端IP地址表(每IP对应一个记录,用来限制每客户的最大连接数 */
	IHTTPServerStatusHandler *_statusHandler; /*状态回调接口,实现这个接口可以获得服务器运行中的状态 */
	Lock _lock; /* Windows同步控制对象 */
	int _tmpFileNameNo; /* 临时文件名序号 */
	char _tmpFileNamePre[5]; /* 临时文件名的前缀(用来**尽可能**避免多个HTTPServer共享同一个临时目录时的命名冲突即使系统本身可以做到) */
	 
	/*
	* 内部使用的工具函数.
	*/
	connection_context_t* allocConnectionContext(const std::string &strIP, unsigned int nPort); /*初始化一个客户描述符.*/
	void freeConnectionContext(connection_context_t* client);	/*回收客户描述符.*/
	int initListenSocket(const std::string& strIP, int nPort, SOCKET& hListenSock); /*初始化侦听套接字*/
	void doStop();	/*回收服务器资源,在Run()调用失败的情况下调用.*/
	int doAccept(); /*创建一个新的套接字,并执行调用AcceptEx*/
	void doRequestDone(connection_context_t* conn, int status);
	void doConnectionClosed(connection_context_t* conn, int status);

	/*
	* IOCPCallback 接收到网络事件后根据操作类型,派发到具体处理网络事件的函数.
	*/
	static void IOCPCallback(iocp_key_t s, int flags, bool result, int transfered, byte* buf, size_t len, void* param);
	void onAccept(bool sucess);
	
public:
	HTTPServer();
	~HTTPServer();

	/*
	* 设置信息
	*/
	bool mapServerFilePath(const std::string& url, std::string& serverPath);
	std::string tmpFileName();
	inline const std::string& docRoot() { return _docRoot; }
	inline bool isDirectoryVisible() { return _isDirectoryVisible; }
	inline const std::string& defaultFileNames() { return _dftFileName; }
	inline const std::string& ip() { return _ip; }
	inline u_short port() { return _port; }
	inline size_t maxConnectionsPerIp() { return _maxConnectionsPerIp; }
	inline size_t maxConnections() { return _maxConnections; }
	inline size_t maxConnectionSpeed() { return _maxConnectionSpeed; }
	inline unsigned long sessionTimeout() { return _sessionTimeout; }
	inline unsigned long recvTimeout() { return _recvTimeout; }
	inline unsigned long sendTimeout() { return _sendTimeout; }
	inline unsigned long keepAliveTimeout() { return _keepAliveTimeout; }

	/*
	* 回调函数
	*/
	virtual int onRequestDataReceived(IRequest* request, size_t bytesTransfered);
	virtual int onResponderDataSent(IResponder *responder, size_t bytesTransfered);
	virtual void onRequest(IRequest* request, int status);
	virtual void onResponder(IResponder *responder, int status);

	/* 
	* 启动或者停止服务器
	*/
	int run(IHTTPConfig *conf, IHTTPServerStatusHandler *statusHandler);
	int stop();
	inline bool runing() { return _isRuning; }
};

#endif

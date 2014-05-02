/* Copyright (C) 2011 阙荣文
 *
 * 这是一个开源免费软件,您可以自由的修改和发布.
 * 禁止用作商业用途.
 *
 * 联系原作者: querw@sina.com 
*/

#include "stdafx.h"
#include <assert.h>
#include <io.h>
#include "HttpServer.h"
#include "HTTPRequest.h"
#include "HTTPResponder.h"
#include "FCGIResponder.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
HTTPServer::HTTPServer()
	: _isRuning(false),
	_sListen(IOCP_NULLKEY),
	_sockNewClient(INVALID_SOCKET),
	_statusHandler(NULL),
	_hrt(true), 
	_fcgiFactory(NULL),
	_tmpFileNameNo(0)
{
	/*
	* 额外的16个字节是 AcceptEx()的要求,见MSDN.
	*/
	_acceptContext.len = (sizeof(sockaddr_in) + 16) * 2;  
	_acceptContext.buf = new byte[_acceptContext.len];
}

HTTPServer::~HTTPServer()
{
	assert(_sListen == IOCP_NULLKEY);
	assert(_sockNewClient == INVALID_SOCKET);

	delete []_acceptContext.buf;
}

HTTPServer::connection_context_t* HTTPServer::allocConnectionContext(const std::string &ipAddr, unsigned int port)
{
	connection_context_t* conn = new connection_context_t;
	assert(conn);
	if( NULL == conn ) return NULL;
	memset(conn, 0, sizeof(connection_context_t));

	/*
	* 创建 请求对象.
	*/
	conn->request = new HTTPRequest(this, &_network);

	/*
	* 记录状态.
	*/
	conn->startTime = _hrt.now();

	assert(ipAddr.size() <= MAX_IP_LENGTH);
	strcpy(conn->ip, ipAddr.c_str());
	conn->port = port;

	return conn;
}

void HTTPServer::freeConnectionContext(connection_context_t* conn)
{
	if(NULL == conn) 
	{
		assert(0);
		return;
	}

	/*
	* 从IOCP模块中移除并且关闭套接字.
	*/
	if(conn->clientSock != IOCP_NULLKEY)
	{
		assert(0);
		_network.remove(conn->clientSock);
	}
	if(conn->request)
	{
		conn->request->reset();
		delete conn->request;
	}
	if(conn->responder)
	{
		conn->responder->reset();
		delete conn->responder;
	}

	delete conn;
}

bool HTTPServer::mapServerFilePath(const std::string& orgUrl, std::string& serverPath)
{
	// 对url进行 utf-8 解码
	std::string deUrl = decode_url(orgUrl);

	// 获得根目录
	serverPath = _docRoot;
	if(serverPath.back() == '\\') serverPath.erase(--serverPath.end());

	// 与 URL 中的路径部分(参数部分忽略)合并获得完整路径
	std::string::size_type pos = orgUrl.find('?');
	if( pos != std::string::npos )
	{
		serverPath += orgUrl.substr(0, pos);
	}
	else
	{
		serverPath += deUrl;
	}
	
	// URL的正斜杠替换为反斜杠.
	for(std::string::iterator iter = serverPath.begin(); iter != serverPath.end(); ++iter)
	{
		if( *iter == '/' ) *iter = '\\'; 
	}

	// 如果是目录名并且不允许浏览目录,则尝试添加默认文件名
	if(serverPath.back() == '\\' && !isDirectoryVisible())
	{
		// 禁止浏览目录,先尝试打开默认文件
		bool hasDftFile = false;
		str_vec_t dftFileNames;
		split_strings(defaultFileNames(), dftFileNames, ",");
		for(str_vec_t::iterator iter = dftFileNames.begin(); iter != dftFileNames.end(); ++iter)
		{
			std::string dftFilePath(serverPath);
			dftFilePath += *iter;
			if(WINFile::exist(AtoT(dftFilePath).c_str()))
			{
				serverPath += *iter;
				hasDftFile = true;
				break;
			}
		}

		return hasDftFile;
	}
	else
	{
		return true;
	}
}

std::string HTTPServer::tmpFileName()
{
	char fileName[MAX_PATH + 1] = {0};
	if( 0 == GetTempFileNameA(_tmpRoot.c_str(), _tmpFileNamePre, 0, fileName))
	{
		assert(0);

		/* 无法获取临时文件名,则按序号生成一个 */
		int no = 0;
		_lock.lock();
		no = ++_tmpFileNameNo;
		_lock.unlock();

		std::stringstream fmt;

		if(_tmpRoot.back() == '\\')
		{
			fmt << _tmpRoot << no << ".tmp";
		}
		else
		{
			fmt << _tmpRoot << '\\' << no << ".tmp";
		}

		return fmt.str();
	}
	else
	{
		return fileName;
	}
}
/*
* 初始化侦听套接字
*/
int HTTPServer::initListenSocket(const std::string& strIP, int nPort, SOCKET& hListenSock)
{
	SOCKET hSock = INVALID_SOCKET;

	/*
	* 创建套接字,设置为非阻塞模式,并且绑定侦听端口.
	*/
	if( (hSock = socket(PF_INET, SOCK_STREAM, /*IPPROTO_TCP*/0 )) == INVALID_SOCKET )
	{
		LOGGER_CERROR(theLogger, _T("无法创建侦听套接字.\r\n"));
		return SE_CREATESOCK_FAILED;
	}

	u_long nonblock = 1;
	ioctlsocket(hSock, FIONBIO, &nonblock);
	
	sockaddr_in addr;
	addr.sin_family	= AF_INET;
	addr.sin_port = htons(nPort);
	if( strIP == "" )
	{
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
	}
	else
	{
		addr.sin_addr.s_addr = inet_addr(strIP.c_str());
	}
	if( 0 != bind(hSock, (sockaddr *)&addr, sizeof(sockaddr_in)) )
	{
		LOGGER_CERROR(theLogger, _T("侦听套接字无法绑定端口:%d.\r\n"), nPort);
		closesocket(hSock);
		return SE_BIND_FAILED;
	}

	if( 0 != listen(hSock, 10))
	{
		LOGGER_CERROR(theLogger, _T("侦听套接字无法侦听,错误码:%d.\r\n"), WSAGetLastError());
		closesocket(hSock);
		return SE_LISTEN_FAILED;
	}
	else
	{
		hListenSock = hSock;
		return SE_SUCCESS;
	}
}

int HTTPServer::run(IHTTPConfig *conf, IHTTPServerStatusHandler *statusHandler)
{
	int ret = SE_SUCCESS;

	do
	{
		/*
		* 服务器已经在运行
		*/
		if(runing())
		{
			assert(0);
			ret = SE_RUNING;
			LOGGER_CWARNING(theLogger, _T("已经在运行.\r\n"));
			break;
		}

		/*
		* 初始化ServerEnv
		*/
		_docRoot = conf->docRoot(); /*根目录*/
		_tmpRoot = conf->tmpRoot();
		_isDirectoryVisible = conf->dirVisible(); /*是否允许浏览目录*/
		_dftFileName = conf->defaultFileNames(); /*默认文件名*/
		_ip = conf->ip(); /*服务器IP地址*/
		_port = conf->port(); /*服务器侦听端口*/
		_maxConnections = conf->maxConnections(); /*最大连接数*/
		_maxConnectionsPerIp = conf->maxConnectionsPerIp(); /*每个IP的最大连接数*/
		_maxConnectionSpeed = conf->maxConnectionSpeed(); /*每个连接的速度限制,单位 b/s.*/

		_sessionTimeout = conf->sessionTimeout(); /*会话超时*/
		_recvTimeout = conf->recvTimeout(); /*recv, connect, accept 操作的超时*/
		_sendTimeout = conf->sendTimeout(); /*send 操作的超时*/
		_keepAliveTimeout = conf->keepAliveTimeout();

		_statusHandler = statusHandler;

		/*
		* 设置最多允许使用 fopen() 打开的文件数
		*/
		//if(_maxConnections > MAX_STDIO) _maxConnections = MAX_STDIO;
		//_setmaxstdio(_maxConnections);
		assert(_maxConnections <= MAX_WINIO);
	
		/*
		* 初始化网络模块
		*/
		if(IOCP_SUCESS != _network.init(0))
		{
			assert(0);
			ret = SE_NETWORKFAILD;
			LOGGER_CERROR(theLogger, _T("无法初始化网络.\r\n"));
			break;
		}

		/*
		* 初始化侦听套接字,并注册到IOCP网络模块中准备接收新连接.
		*/
		SOCKET sockListen = INVALID_SOCKET;
		int lsRet = initListenSocket(_ip, _port, sockListen);
		if( SE_SUCCESS != lsRet )
		{
			ret = lsRet;
			break;
		}
		_sListen = _network.add(sockListen, 0, 0, 0);

		/*
		* 初始化 Fast CGI 模块
		* 目前只支持一个 FCGI 服务器,把 _fcgiFactory 改为队列很容易扩展.
		*/
		assert(_fcgiFactory == NULL);
		fcgi_server_t fcgiServerInf;
		if(conf->getFirstFcgiServer(&fcgiServerInf) && fcgiServerInf.status)
		{
			_fcgiFactory = new FCGIFactory(this, &_network);
			_fcgiFactory->init(fcgiServerInf.path, fcgiServerInf.port, fcgiServerInf.exts, fcgiServerInf.maxConnections, 
				fcgiServerInf.maxWaitListSize == 0 ? SIZE_T_MAX : fcgiServerInf.maxWaitListSize, fcgiServerInf.cacheAll);
		}
	
		/*
		* 记录状态
		*/
		_isRuning = true;
		_connections.clear();
		_connectionIps.clear();

		/*
		* 临时文件名生成参数
		*/
		_tmpFileNameNo = 0;
		srand(static_cast<unsigned int>(time(NULL)));
		sprintf(_tmpFileNamePre, "%03d", rand() % 1000);

		/* 
		* 执行第一次accept() 
		*/
		int acceptRet = doAccept();
		if(acceptRet != SE_SUCCESS)
		{
			assert(0);
			ret = acceptRet;
			break;
		}

		/*
		* 成功的出口
		*/
		std::string ipAddress = ip();
		if(ipAddress == "")
		{
			get_ip_address(ipAddress);
		}
		LOGGER_CINFO(theLogger, _T("Q++ HTTP Server 正在运行,根目录[%s],地址[%s:%d],最大连接数[%d].\r\n"), 
			AtoT(docRoot()).c_str(), AtoT(ipAddress).c_str(), port(), maxConnections());

		return SE_SUCCESS;
	}while(0);

	/*
	* 失败的出口
	*/
	doStop();
	LOGGER_CWARNING(theLogger, _T("Q++ HTTP Server 启动失败.\r\n"));
	return ret;
}

int HTTPServer::doAccept()
{
	assert(_sListen != IOCP_NULLKEY);

	/*
	* 创建一个新的套接字.
	*/
	_sockNewClient = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if( INVALID_SOCKET == _sockNewClient )
	{
		assert(0);
		LOGGER_CERROR(theLogger, _T("无法创建套接字,错误码:%d.\r\n"), WSAGetLastError());
		return SE_CREATESOCK_FAILED;
	}

	/*
	* 调用AcceptEx()函数
	*/
	if(IOCP_PENDING != _network.accept(_sListen, _sockNewClient, _acceptContext.buf, _acceptContext.len, 0, IOCPCallback, this))
	{
		closesocket(_sockNewClient);
		_sockNewClient = INVALID_SOCKET;

		assert(0);
		LOGGER_CERROR(theLogger, _T("执行accept失败,无法接受新连接,错误码:%d.\r\n"), _network.getLastError());
		return SE_NETWORKFAILD;
	}
	else
	{
		return SE_SUCCESS;
	}
}

void HTTPServer::doStop()
{
	/*
	* 按照初始化的反顺序清理
	*/

	/*
	* 释放所有FCGI资源,清理Fast CGI 模块
	*/
	if(_fcgiFactory)
	{
		_fcgiFactory->destroy();
	}

	/*
	* 停止网络模块,使IOCP回调函数不再被调用.
	* 关闭侦听套接字和为接受新连接而准备的套接字.
	*/
	if(IOCP_SUCESS != _network.destroy())
	{
		assert(0);
		LOGGER_CFATAL(theLogger, _T("无法停止网络模块,错误码[%d].\r\n"), _network.getLastError());
	}
	if(IOCP_NULLKEY != _sListen)
	{
		_sListen = IOCP_NULLKEY;
	}
	if(INVALID_SOCKET != _sockNewClient)
	{
		closesocket(_sockNewClient);
		_sockNewClient = INVALID_SOCKET;
	}

	/*
	* 释放剩余未断开的客户端连接,清空客户IP表.
	* 最后才能释放剩下的套接字和关联数据,
	* 不需要加锁,运行到这里,所有定时器,线程都已经停止运行,不再会有资源竞争.
	*/
	if(_connections.size() > 0)
	{
		LOGGER_CWARNING(theLogger, _T("退出时还有:[%d]个连接,将强制关闭.\r\n"), _connections.size());
	}
	for(connection_map_t::iterator iter = _connections.begin(); iter != _connections.end(); ++iter)
	{
		connection_context_t* conn = iter->second;
		conn->clientSock = IOCP_NULLKEY;
		freeConnectionContext(conn);
	}
	_connections.clear();
	
	delete _fcgiFactory;
	_fcgiFactory = NULL;
	_statusHandler = NULL;
	_connectionIps.clear();
	_isRuning = false;
}

int HTTPServer::stop()
{
	if(!runing()) return SE_STOPPED;
	doStop();

	LOGGER_CINFO(theLogger, _T("Q++ HTTP Server 停止.\r\n"));
	return SE_SUCCESS;
}

void HTTPServer::onAccept(bool sucess)
{
	/*
	* 失败的accept调用,丢弃,重新来一次.
	*/
	if(!sucess)
	{
		LOGGER_CWARNING(theLogger, _T("捕捉到一次失败的AcceptEx调用,错误码[%d].\r\n"), WSAGetLastError());
		shutdown(_sockNewClient, SD_BOTH);
		closesocket(_sockNewClient);
		_sockNewClient = INVALID_SOCKET;
		doAccept();
		return;
	}

	/*
	* 新客户连接, 获取客户IP等信息, 更新套接字信息,使 getsockname() 和 getpeername() 可用.
	* 详见MSDN关于 AcceptEx 的说明.
	*/
	SOCKET sockListen = _network.getSocket(_sListen);
	if( 0 != setsockopt( _sockNewClient, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char *)&sockListen, sizeof(sockListen)) )
	{
		LOGGER_CERROR(theLogger, _T("无法更新套接字信息,错误码[%d].\r\n"), WSAGetLastError());
	}

	/* 
	* 获得新客户IP和端口
	*/
	sockaddr_in clientAddr;
	int nAddrLen = sizeof(sockaddr_in);
	if( 0 != getpeername(_sockNewClient, (sockaddr *)&clientAddr, &nAddrLen) )
	{
		LOGGER_CERROR(theLogger, _T("无法获取客户端地址和端口,错误码[%d].\r\n"), WSAGetLastError());
	}
	std::string clientIp = inet_ntoa(clientAddr.sin_addr);
	unsigned int clientPort = ntohs(clientAddr.sin_port);

	/*
	* 创建连接对象.
	*/
	SOCKET sNewClient = _sockNewClient;
	_sockNewClient = INVALID_SOCKET;
	connection_context_t* conn = NULL;
	bool kicked = false;
	bool refused = false;

	_lock.lock();
	do 
	{
		/*
		* 查看是否已经达到最大连接.
		*/
		if(_connections.size() >= this->maxConnections())
		{
			/*
			* 已经达到最大连接数,直接关闭套接字.
			*/
			refused = true;
			break;
		}
		
		/*
		* 单个IP的最大连接数是否达到上限,是则踢出.
		*/
		str_int_map_t::iterator iterClientIP = _connectionIps.end();
		if(this->maxConnectionsPerIp() > 0)
		{
			iterClientIP = _connectionIps.find(clientIp);
			if(iterClientIP != _connectionIps.end() && iterClientIP->second >= this->maxConnectionsPerIp())
			{
				kicked = true;
				break;
			}
		}

		/*
		* 为新客户分配资源.
		*/
		conn = allocConnectionContext(clientIp, clientPort);
		conn->clientSock = _network.add(sNewClient, this->sessionTimeout(), 0, this->maxConnectionSpeed());
		_connections.insert(std::make_pair(conn->clientSock, conn));
		if(this->maxConnectionsPerIp() > 0)
		{
			/*
			* 记录当前客户一共已经有多少个连接了.
			*/
			if(iterClientIP != _connectionIps.end()) iterClientIP->second++;
			else _connectionIps.insert(std::make_pair(clientIp, 1));
		}
	} while(false);
	_lock.unlock();

	/*
	* 用连接对象开始接收HTTP请求
	*/
	if( conn != NULL )
	{
		/* 接受新连接 */
		LOGGER_CINFO(theLogger, _T("[%s:%d] - 新连接被接受.\r\n"), AtoT(clientIp).c_str(), clientPort);
		
		/* 发送状态 */
		if(_statusHandler)
		{
			_statusHandler->onNewConnection(clientIp.c_str(), clientPort, false, false);
		}

		/* 开始接收 */
		assert(conn->request);
		int ret = conn->request->run(conn, conn->clientSock, recvTimeout());
		if(CT_SUCESS != ret)
		{
			/* 接收失败 */
			doConnectionClosed(conn, ret);
		}
		else
		{
			/* 
			* 接收成功, 后面不应再有访问 conn 的代码, 由于是网络事件驱动的模型, 
			* conn->request->recv() 成功后,不保证 conn 对象的有效性.
			*/
		}
	}
	else
	{
		/* 新连接被拒绝 */
		if(refused)
		{
			LOGGER_CWARNING(theLogger, _T("[%s:%d] - 服务器已经达到最大连接数,连接被丢弃.\r\n"), AtoT(clientIp).c_str(), clientPort);
		}
		else if(kicked)
		{
			LOGGER_CWARNING(theLogger, _T("[%s:%d] - 客户连接被拒绝,超出单IP最大允许的连接数限制.\r\n"), AtoT(clientIp).c_str(), clientPort);
		}
		else
		{
			assert(0);
		}

		/* 发送状态 */
		if(_statusHandler)
		{
			_statusHandler->onNewConnection(clientIp.c_str(), clientPort, refused, kicked);
		}

		/* 关闭套接字 */
		shutdown(sNewClient, SD_BOTH);
		closesocket(sNewClient);
	}

	
	/*
	* 准备接收下一个连接
	*/
	doAccept();
}

/*
* responder 处理完毕或者失败后调用 doRequestDone 表示一个请求已经处理完毕.
* 所以 conn->responder 一定是有效的.
*/
void HTTPServer::doRequestDone(connection_context_t* conn, int status)
{
	assert(conn->request);
	assert(conn->responder);
	
	/*
	* 记录必要的数据发送到状态接口
	*/
	__int64 bytesSent = 0;
	__int64 bytesRecv = conn->request->size();
	if(conn->responder)
	{
		bytesRecv += conn->responder->getTotalRecvBytes();
		bytesSent += conn->responder->getTotalSentBytes();
	}
	size_t totalTime = conn->request->startTime() == 0 ? 0 : static_cast<unsigned int>(_hrt.getMs(_hrt.now() - conn->request->startTime()));
	std::string clientIp(conn->ip);
	unsigned int clinetPort = conn->port;
	std::string uri = conn->request->uri(true);
	int svrCode = conn->responder->getServerCode();

	/*
	* 写日志记录一个请求已经处理完毕.
	*/
	std::string strBytes = format_size(bytesSent);
	std::string strSpeed = format_speed(bytesSent, totalTime);
	LOGGER_CTRACE(theLogger, _T("[%s:%d] - 响应头:\r\n%s"), AtoT(clientIp).c_str(), clinetPort, AtoT(conn->responder->getHeader()).c_str());
	LOGGER_CINFO(theLogger, _T("[%s:%d] - [%s]处理%s[HTTP %d],发送数据[%s],用时[%.3fs],平均速度[%s].\r\n"), 
		AtoT(clientIp).c_str(), clinetPort, AtoT(uri).c_str(), status == CT_SENDCOMPLETE ? _T("完毕") : _T("终止"), svrCode,
		AtoT(strBytes).c_str(), totalTime * 1.0 / 1000, AtoT(strSpeed).c_str());

	if(_statusHandler)
	{
		_statusHandler->onRequestEnd(clientIp.c_str(), clinetPort, uri.c_str(), svrCode,
			bytesSent, bytesRecv, totalTime, status == CT_SENDCOMPLETE);
	}

	/*
	* 如果 客户端要求 keep-alive ,则保留连接
	*/
	if(status == CT_SENDCOMPLETE && conn->request->keepAlive())
	{
		/* 重置connection的状态 */
		assert(conn->responder);
		conn->request->reset();
		conn->responder->reset();
		delete conn->responder;
		conn->responder = NULL;

		_network.refresh(conn->clientSock);

		/* 开始接收下一个请求 */
		int ret = conn->request->run(conn, conn->clientSock, keepAliveTimeout());
		if(CT_SUCESS != ret)
		{
			/* 接收失败 */
			doConnectionClosed(conn, ret);
		}
		else
		{
			/* 
			* 接收成功, 后面不应再有访问 conn 的代码, 由于是网络事件驱动的模型, 
			* conn->request->recv() 成功后,不保证 conn 对象的有效性.
			*/
		}
	}
	else
	{
		doConnectionClosed(conn, status);
	}
}

/*
* 关闭连接,可能是成功处理请求后关闭,也可能是响应失败后关闭或者请求接收失败.
* conn->request 是有效的,但是请求头不一定完整.
*/
void HTTPServer::doConnectionClosed(connection_context_t* conn, int status)
{
	/*
	* 先记录数据
	* 连接的IP地址,端口和该连接总共占用的时间
	*/
	std::string clientIp(conn->ip);
	unsigned int clinetPort = conn->port;
	unsigned int connTime = static_cast<unsigned int>(_hrt.getMs(_hrt.now() - conn->startTime));

	/*
	* 先停止关于该连接的所有调用,确保安全后才能回收资源.
	* 1. 延时操作工作线程
	* 2. 网络模块
	*/
	_network.remove(conn->clientSock);
	//TRACE("HTTPServer::doRequestDone() remove iocp key 0x%x.\r\n", conn->clientSock);

	/*
	* 从队列中清除,并且清除IP表.
	*/
	_lock.lock();
	_connections.erase(conn->clientSock);
	if(this->maxConnectionsPerIp() > 0)
	{
		str_int_map_t::iterator iter = _connectionIps.find(conn->ip);
		if(iter != _connectionIps.end())
		{
			if( --(iter->second) <= 0) _connectionIps.erase(iter);
		}
	}
	_lock.unlock();

	conn->clientSock = IOCP_NULLKEY;
	freeConnectionContext(conn);

	/*
	* 日志和状态通知
	*/
	std::string txt("");
	switch(status)
	{
	case CT_CLIENTCLOSED: { txt = "客户端关闭了连接"; break; }
	case CT_SENDCOMPLETE: { txt = "数据发送完毕"; break; }
	case CT_SEND_TIMEO: { txt = "发送超时"; break; }
	case CT_RECV_TIMEO: { txt = "接收超时"; break; }
	case CT_SESSION_TIMEO: { txt = "会话超时"; break; }
	case CT_FCGI_CONNECT_FAILED: { txt = "无法连接到FCGI服务器"; break; }
	default: { txt = "未知"; break; }
	}
	LOGGER_CINFO(theLogger, _T("[%s:%d] - 连接被关闭[%s],总计用时[%.3fs].\r\n"), 
		AtoT(clientIp).c_str(), clinetPort, AtoT(txt).c_str(), connTime * 1.0 / 1000);
	if(_statusHandler)
	{
		_statusHandler->onConnectionClosed(clientIp.c_str(), clinetPort, static_cast<HTTP_CLOSE_TYPE>(status));
	}
}


int HTTPServer::onRequestDataReceived(IRequest* request, size_t bytesTransfered)
{
	connection_context_t *conn = reinterpret_cast<connection_context_t *>(request->getConnectionId());

	/* 通知状态处理接口,以便统计带宽 */
	if(_statusHandler)
	{
		_statusHandler->onDataReceived(conn->ip, conn->port, bytesTransfered);
	}
	return 0;
}

int HTTPServer::onResponderDataSent(IResponder *responder, size_t bytesTransfered)
{
	connection_context_t *conn = reinterpret_cast<connection_context_t *>(responder->getConnectionId());

	/* 通知状态处理接口,以便统计带宽 */
	if(_statusHandler)
	{
		_statusHandler->onDataSent(conn->ip, conn->port, bytesTransfered);
	}
	return 0;
}

void HTTPServer::onRequest(IRequest* request, int status)
{
	connection_context_t* conn = reinterpret_cast<connection_context_t*>(request->getConnectionId());
	if(status != CT_SUCESS)
	{
		/* 接收超时或者失败,无法接收请求头,直接关闭连接 */
		doConnectionClosed(conn, status);
		return;
	}

	/*
	* 已经接收到一个请求,生成一个响应.
	*/
	std::string serverFileName;
	std::string uri = request->uri(true);

	/*
	* 接收到一个请求头,trace
	*/
	char method[100];
	map_method(request->method(), method);
	LOGGER_CTRACE(theLogger, _T("[%s:%d] - 请求头:\r\n%s"), AtoT(conn->ip).c_str(), conn->port, AtoT(request->getHeader()).c_str());
	LOGGER_CINFO(theLogger, _T("[%s:%d] - [%s][%s]...\r\n"), AtoT(conn->ip).c_str(), conn->port, AtoT(method).c_str(), AtoT(uri).c_str());  
	if(_statusHandler)
	{
		_statusHandler->onRequestBegin(conn->ip, conn->port, uri.c_str(),request->method());
	}
	
	/* 开始处理请求 */
	do
	{
		/* 映射为服务器文件名 */
		if(mapServerFilePath(uri, serverFileName))
		{
			/* 生成一个Responder对象 */
			if(conn->request->isValid())
			{
				if(_fcgiFactory && _fcgiFactory->catchRequest(serverFileName))
				{
					/* 生成一个FCGI响应 */
					conn->responder = new FCGIResponder(this, &_network, _fcgiFactory);
					break;
				}
			}
		}
	
		/* 生成一个静态响应用于处理默认请求 */
		assert(conn->responder == NULL);
		conn->responder = new HTTPResponder(this, &_network);
	}while(0);

	/*
	* 发送响应
	*/
	int ret = conn->responder->run(conn, conn->clientSock, conn->request);
	if(CT_SUCESS != ret)
	{
		doRequestDone(conn, ret);
	}
	else
	{
		/* 
		* 后面不应该再有任何代码访问 conn, 事件驱动模式下,无法保证 conn 的有效性 
		*/
	}
}

void HTTPServer::onResponder(IResponder *responder, int status)
{
	/*
	* 发送完毕或者出错
	*/
	connection_context_t* conn = reinterpret_cast<connection_context_t*>(responder->getConnectionId());
	doRequestDone(conn, status);
}

/*
* IOCP网络模块的回调函数.
*/
void HTTPServer::IOCPCallback(iocp_key_t s, int flags, bool result, int transfered, byte* buf, size_t len, void* param)
{

	HTTPServer* instPtr = reinterpret_cast<HTTPServer*>(param);

	assert(flags & IOCP_ACCEPT);

	/*
	* accpet 回调.
	*/
	instPtr->onAccept(result);
}

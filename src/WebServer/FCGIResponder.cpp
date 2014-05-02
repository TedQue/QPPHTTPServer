#include "StdAfx.h"
#include "FCGIResponder.h"

#define FCGI_INIT_TMPBUF_SIZE 2048

#define DSS_NONE 0 /* 数据流状态: 未设置 */
#define DSS_RUNING 0x01 /* 数据流状态: 正在运行 */
#define DSS_ABORTED 0x02 /* 数据流状态: 已出错停止 */
#define DSS_COMPLETE 0x04 /* 数据流状态: 已完成 */
#define DSS_PAUSED 0x08 /* 数据流状态: 暂停 */

FCGIResponder::FCGIResponder(IHTTPServer *server, IOCPNetwork *network, FCGIFactory *fcgiFactory)
	: _request(NULL), _bytesRecv(0), _bytesSent(0), _isFCGIHeaderReceived(false),
	_server(server), _network(network), _clientSock(IOCP_NULLKEY), _connId(NULL), _svrCode(SC_UNKNOWN),
	_fcgiFactory(fcgiFactory), _bytesFCGISent(0), _bytesFCGIRecv(0), _fcgiConnection(NULL), _cacheAll(false),
	_fcgiRecvBuf(NULL), _fcgiSendBuf(NULL), _postDataBuf(NULL), _stdoutRecord(NULL), _chunkCoding(false), _chunkEndSent(false),
	_httpSendBuf(NULL), _cache(NULL), _httpErrorCache(NULL), _exitCode(CT_UNKNOWN)
{

}

FCGIResponder::~FCGIResponder()
{

}

FCGIResponder::buffer_t* FCGIResponder::allocBuffer()
{
	buffer_t *buf = new buffer_t;
	buf->buf = new byte[FCGI_BUFFER_SIZE];
	buf->len = 0;
	buf->size = FCGI_BUFFER_SIZE;
	return buf;
}

void FCGIResponder::freeBuffer(buffer_t *buf)
{
	if( buf )
	{
		if( buf->buf ) delete []buf->buf;
		delete buf;
	}
}

/*
* 入口函数
*/
int FCGIResponder::run(conn_id_t connId, iocp_key_t clientSock, IRequest *request)
{
	/* 保存参数 */
	_connId = connId;
	_clientSock = clientSock;
	_request = request;
	
	/* 检查参数 */
	assert(_fcgiFactory);
	fcgi_conn_t *conn = NULL;
	int ret = CT_SUCESS;

	/* 获取 FCGI 连接 */
	if( _fcgiFactory->getConnection(conn, onFcgiConnectionReady, this))
	{
		if(conn != NULL)
		{
			/* 获得一个可用的连接 */
			_fcgiConnection = conn;
			
			/* 准备连接到FCGI服务器(远程)或者发送FCGI_BEGIN_REQUEST(本地) */
			ret = initFCGIEnv();
		}
		else
		{
			/* 等待回调 */
			ret = CT_SUCESS;
		}
	}
	else
	{
		ret = CT_INTERNAL_ERROR;
	}

	if(CT_SUCESS != ret)
	{
		reset();
	}
	return ret;
}

bool FCGIResponder::stop(int ec)
{
	return false;
}

bool FCGIResponder::reset()
{
	/*
	* 回收资源
	*/
	if(_fcgiConnection)
	{
		_fcgiFactory->releaseConnection(_fcgiConnection, false);
		_fcgiConnection = NULL;
	}

	_connId = INVALID_CONNID;

	freeBuffer(_fcgiRecvBuf);
	freeBuffer(_httpSendBuf);
	freeBuffer(_postDataBuf);
	if( _fcgiSendBuf ) delete _fcgiSendBuf;
	_postDataBuf = NULL;
	_fcgiSendBuf = NULL;
	_fcgiRecvBuf = NULL;
	_httpSendBuf = NULL;

	if(_stdoutRecord)
	{
		delete _stdoutRecord;
		_stdoutRecord = NULL;
	}

	if(_cache)
	{
		delete _cache;
		_cache = NULL;
	}

	if(_httpErrorCache)
	{
		delete _httpErrorCache;
		_httpErrorCache = NULL;
	}

	_bytesFCGIRecv = 0;
	_bytesFCGISent = 0;
	_bytesRecv = 0;
	_bytesSent = 0;

	return true;
}

/*
* 初始化 FCGI 的运行环境
*/
int FCGIResponder::initFCGIEnv()
{
	/*
	* 初始化内部状态
	* 分配缓存
	* 分配数据流1 [HTTP -> FCGI] 缓冲区
	*/
	assert(_cache == NULL && _fcgiSendBuf == NULL);
	_cacheAll = _fcgiConnection->cacheAll;
	if(_cache == NULL)
	{
		_cache = new FCGICache(FCGI_CACHE_SIZE, _server->tmpFileName());
	}
	if(_fcgiSendBuf == NULL)
	{
		_fcgiSendBuf = new memfile();
	}
	
	/* 
	* 准备参数 
	*/
	std::string tmp;
	char numberBuf[50] = {0};

	/* 分析uri信息 */
	std::string uri = _request->uri(false);
	std::string uriPath(""), uriQueryString("");
	std::string uriServerPath;
	std::string::size_type pos = uri.find('?');
	if(pos == std::string::npos)
	{
		uriPath = uri;
	}
	else
	{
		uriPath = uri.substr(0, pos);
		uriQueryString = uri.substr(pos + 1, uri.size() - pos - 1);
	}

	if(!_server->mapServerFilePath(uriPath, uriServerPath))
	{
		assert(0);
		return CT_INTERNAL_ERROR;
	}

	/* 获取连接地址 */
	sockaddr_in svrAddr, clientAddr;
	memset(&svrAddr, 0, sizeof(sockaddr_in));
	memset(&clientAddr, 0, sizeof(sockaddr_in));
	int nameLen = sizeof(sockaddr_in);
	if(SOCKET_ERROR == getsockname(_network->getSocket(_clientSock), (sockaddr*)&svrAddr, &nameLen))
	{
		assert(0);
	}
	nameLen = sizeof(sockaddr_in);
	if(SOCKET_ERROR == getpeername(_network->getSocket(_clientSock), (sockaddr*)&clientAddr, &nameLen))
	{
		assert(0);
	}

	/* HTTP方法 */
	char method[50] = {0};
	map_method(_request->method(), method);

	/* SERVER_NAME - HOST */
	std::string hostName = _request->field("Host");
	if( hostName.size() <= 0)
	{
		hostName = _server->ip();
	}
	else
	{
		std::string::size_type pos = hostName.find(':');
		if(pos != std::string::npos)
		{
			hostName = hostName.substr(0, pos);
		}
	}

	/* 
	* 准备缓冲区 
	*/
	FCGIRecordWriter writer(*_fcgiSendBuf);

	/* 发送 FCGI_BEGIN_REQUEST */
	writer.writeHeader(_fcgiConnection->requestId, FCGI_BEGIN_REQUEST);
	writer.writeBeginRequestBody(FCGI_RESPONDER, true);
	writer.writeEnd();

	/* 发送参数 */
	writer.writeHeader(_fcgiConnection->requestId, FCGI_PARAMS);
	writer.writeNameValuePair("HTTPS", "off");
	writer.writeNameValuePair("REDIRECT_STATUS", "200");
	writer.writeNameValuePair("SERVER_PROTOCOL", "HTTP/1.1");
	writer.writeNameValuePair("GATEWAY_INTERFACE", "CGI/1.1");
	writer.writeNameValuePair("SERVER_SOFTWARE", SERVER_SOFTWARE);
	writer.writeNameValuePair("SERVER_NAME", hostName.c_str());
	writer.writeNameValuePair("SERVER_ADDR", inet_ntoa(svrAddr.sin_addr));
	writer.writeNameValuePair("SERVER_PORT", itoa(ntohs(svrAddr.sin_port), numberBuf, 10));
	writer.writeNameValuePair("REMOTE_ADDR", inet_ntoa(clientAddr.sin_addr));
	writer.writeNameValuePair("REMOTE_PORT", itoa(ntohs(clientAddr.sin_port), numberBuf, 10));
	writer.writeNameValuePair("REQUEST_METHOD", method);
	writer.writeNameValuePair("REQUEST_URI", uri.c_str());
	if(uriQueryString.size() > 0) writer.writeNameValuePair("QUERY_STRING",uriQueryString.c_str());

	writer.writeNameValuePair("DOCUMENT_ROOT", _server->docRoot().c_str());
	writer.writeNameValuePair("SCRIPT_NAME", uriPath.c_str());
	writer.writeNameValuePair("SCRIPT_FILENAME", uriServerPath.c_str());
	//writer.writeNameValuePair("PATH_INFO", uriServerPath.c_str());
	//writer.writeNameValuePair("PATH_TRANSLATED", uriServerPath.c_str());

	writer.writeNameValuePair("HTTP_HOST", _request->field("Host").c_str());
	writer.writeNameValuePair("HTTP_USER_AGENT", _request->field("User-Agent").c_str());
	writer.writeNameValuePair("HTTP_ACCEPT", _request->field("Accept").c_str());
	writer.writeNameValuePair("HTTP_ACCEPT_LANGUAGE", _request->field("Accept-Language").c_str());
	writer.writeNameValuePair("HTTP_ACCEPT_ENCODING", _request->field("Accept-Encoding").c_str());

	tmp = _request->field("Cookie");
	if(tmp.size() > 0)
	{
		writer.writeNameValuePair("HTTP_COOKIE", tmp.c_str());
	}
	
	tmp = _request->field("Referer");
	if(tmp.size() > 0)
	{
		writer.writeNameValuePair("HTTP_REFERER", tmp.c_str());
	}

	tmp = _request->field("Content-Type");
	if(tmp.size() > 0)
	{
		writer.writeNameValuePair("CONTENT_TYPE", tmp.c_str());
	}

	tmp = _request->field("Content-Length");
	if(tmp.size() > 0)
	{
		writer.writeNameValuePair("CONTENT_LENGTH", tmp.c_str());
	}
	writer.writeEnd();

	/* 空记录表示结束 */
	writer.writeHeader(_fcgiConnection->requestId, FCGI_PARAMS);
	writer.writeEnd();

	/* 如果 HTTPRequest 对象不包含 POST 数据,直接发送一个结束标准 */
	if(_request->contentLength() == 0)
	{
		writer.writeHeader(_fcgiConnection->requestId, FCGI_STDIN);
		writer.writeEnd();
	}
	
	/*
	* 把参数发送到 FCGI 服务器
	*/
	return sendToFCGIServer();
}

void FCGIResponder::IOCPCallback(iocp_key_t s, int flags, bool result, int transfered, byte* buf, size_t len, void* param)
{
	FCGIResponder *instPtr = reinterpret_cast<FCGIResponder*>(param);
	
	if( s == instPtr->_clientSock )
	{
		assert(flags & IOCP_SEND);
		instPtr->onHTTPSend(transfered, flags);
	}
	else
	{
		if( flags & IOCP_RECV )
		{
			instPtr->onFCGIRecv(transfered, flags);
		}
		else
		{
			instPtr->onFCGISend(transfered, flags);
		}
	}
}

int FCGIResponder::sendToFCGIServer()
{
	assert(_fcgiSendBuf);
	if( _fcgiConnection->comm == IOCP_NULLKEY) return CT_UNKNOWN;

	if(IOCP_PENDING != _network->send(_fcgiConnection->comm, 
		reinterpret_cast<const byte*>(_fcgiSendBuf->buffer()) + _fcgiSendBuf->tellg(), 
		_fcgiSendBuf->fsize() - _fcgiSendBuf->tellg(), 
		_server->sendTimeout(), IOCPCallback, this))
	{
		DWORD lastErrorCode = _network->getLastError();
		return CT_FCGI_SEND_FAILED;
	}
	else
	{
		return CT_SUCESS;
	}
}

/*
* 从缓存中读取数据并且发送到 HTTP 客户端
*/
int FCGIResponder::sendToHTTPClient()
{
	assert(_httpSendBuf);
	int exitCode = CT_SUCESS;

	/* 读取响应头 */
	_httpSendBuf->len += _header.read(_httpSendBuf->buf + _httpSendBuf->len, _httpSendBuf->size - _httpSendBuf->len);

	/* 读取内容 */
	_httpSendBuf->len += _cache->read(_httpSendBuf->buf + _httpSendBuf->len, _httpSendBuf->size - _httpSendBuf->len);

	/* 发送到客户端 */
	if(_httpSendBuf->len > 0)
	{
		int netIoRet = _network->send(_clientSock, _httpSendBuf->buf, _httpSendBuf->len, _server->sendTimeout(), IOCPCallback, this);
		if( IOCP_PENDING != netIoRet )
		{
			exitCode = (netIoRet == IOCP_SESSIONTIMEO ? CT_SESSION_TIMEO : CT_CLIENTCLOSED);
		}
		else
		{
			exitCode = CT_SUCESS;
		}
	}
	else
	{
		assert(0);
	}
	return exitCode;
}

int FCGIResponder::recvFromFCGIServer()
{
	assert(_fcgiRecvBuf && _fcgiRecvBuf->buf);

	if( IOCP_PENDING != _network->recv(_fcgiConnection->comm, _fcgiRecvBuf->buf, _fcgiRecvBuf->size, 
		_server->recvTimeout(), IOCPCallback, this))
	{
		return CT_FCGI_RECV_FAILED;
	}
	else
	{
	}
	return CT_SUCESS;
}

void FCGIResponder::onConnection(fcgi_conn_t *conn)
{
	if(conn)
	{
		/* 获得了一个可用的连接 */
		_fcgiConnection = conn;

		/* 准备连接到FCGI服务器(远程)或者发送FCGI_BEGIN_REQUEST(本地) */
		int res = initFCGIEnv();
		if(CT_SUCESS != res)
		{
			close(res);
		}
	}
	else
	{
		/* 无法获得连接 发送错误 503  */
		int res = sendHttpError(SC_SERVERBUSY, g_HTTP_Server_Busy);
		if(CT_SUCESS != res) close(res);
	}
}

void FCGIResponder::onFcgiConnectionReady(fcgi_conn_t *conn, void *param)
{
	FCGIResponder *instPtr = reinterpret_cast<FCGIResponder*>(param);
	instPtr->onConnection(conn);
}

/*
* 从临界端出来后,如果已经设置数据流3停止运行,就不能再访问任何类成员,因为另一个线程可能会删除类实例.
* 只能使用函数内的局部变量(栈内分配).
*/
void FCGIResponder::onHTTPSend(size_t bytesTransfered, int flags)
{
	/* 处理 HTTP Error */
	if(_httpErrorCache != NULL)
	{
		if(0 == bytesTransfered)
		{
			close((flags & IOCP_WRITETIMEO) ? CT_SEND_TIMEO : CT_CLIENTCLOSED);
		}
		else
		{
			/* 状态回调 */
			_server->onResponderDataSent(this, bytesTransfered);
			_bytesSent += bytesTransfered;

			/* 把缓冲区内未发送的数据移到开头 */
			_httpSendBuf->len -= bytesTransfered;
			if( _httpSendBuf->len > 0 )
			{
				memmove(_httpSendBuf->buf, _httpSendBuf->buf + bytesTransfered, _httpSendBuf->len);
			}

			/* 从缓存中读取下一段数据 */
			_httpSendBuf->len += _httpErrorCache->read(_httpSendBuf->buf + _httpSendBuf->len, _httpSendBuf->size - _httpSendBuf->len);

			/* 发送到 HTTP 客户端 */
			if( _httpSendBuf->len > 0)
			{
				if( IOCP_PENDING != _network->send(_clientSock, _httpSendBuf->buf, _httpSendBuf->len, _server->sendTimeout(), IOCPCallback, this))
				{
					close(CT_CLIENTCLOSED);
				}
			}
			else
			{
				close(CT_SENDCOMPLETE);
			}
		}
		return;
	}

	/*
	* 处理上一次 IO 操作的结果
	*/
	if(bytesTransfered == 0)
	{
		
	}
	else
	{
		/* 状态回调 */
		_server->onResponderDataSent(this, bytesTransfered);

		_bytesSent += bytesTransfered;

		/* 把上一次没有发完的数据移到缓冲区开头 */
		_httpSendBuf->len -= bytesTransfered;
		if(_httpSendBuf->len > 0)
		{
			memmove(_httpSendBuf->buf, _httpSendBuf->buf + bytesTransfered, _httpSendBuf->len);
		}
	}

	/*
	* 继续进行下一次操作之前有一个退出点
	*/
	bool isClose = false;
	_lock.lock();

	/* 记录上一次操作的结果 */
	if(0 == bytesTransfered)
	{
		_ds3Status = DSS_ABORTED;
		_exitCode = (flags & IOCP_WRITETIMEO) ? CT_SEND_TIMEO : CT_CLIENTCLOSED;
	}

	/*
	* 开始下一个 IO 操作
	*/
	/* 是否继续发送 */
	if(_ds3Status != DSS_ABORTED)
	{
		if(_ds2Status != DSS_ABORTED)
		{
			if(_httpSendBuf->len > 0 || hasData())
			{
				int sendRes = sendToHTTPClient();
				if(CT_SUCESS != sendRes)
				{
					_ds3Status = DSS_ABORTED;
					_exitCode = sendRes;
				}
			}
			else if(_ds2Status == DSS_COMPLETE)
			{
				_ds3Status = DSS_COMPLETE;
				_exitCode = CT_SENDCOMPLETE;
			}
			else
			{
				_ds3Status = DSS_PAUSED;
			}
		}
		else
		{
			/* 数据流2发生了网络错误,数据流3在完成了最后一个IO操作后设置为暂停 */
			_ds3Status = DSS_PAUSED;
		}
	}

	/* 是否退出 */
	isClose = isExitPoint();
	_lock.unlock();

	if(isClose)
	{
		close(_exitCode);
	}
}
bool FCGIResponder::isExitPoint()
{
	/*
	* 什么条件下应该退出
	*/

	/* 退出条件1: 数据流2发生了错误,那么只要数据流3没有在执行 IO 操作就应该退出 */
	if(_ds2Status == DSS_ABORTED && _ds3Status != DSS_RUNING) return true;
	
	/* 退出条件2: 数据流2完成,应该等待数据流3发生网络错误或者完成才退出 */
	if(_ds2Status == DSS_COMPLETE && (_ds3Status == DSS_ABORTED || _ds3Status == DSS_COMPLETE)) return true;
	
	return false;
}

void FCGIResponder::onFCGIRecv(size_t bytesTransfered, int flags)
{
	/* 处理接收到的数据 */
	bool requestEnd = false;
	if(bytesTransfered == 0)
	{
	}
	else
	{
		_bytesFCGIRecv += bytesTransfered;
		_fcgiRecvBuf->len = bytesTransfered;
		requestEnd = parseStdout(_fcgiRecvBuf->buf, _fcgiRecvBuf->len);
	}

	/* 
	* 进行下一次操作之前,检查退出点
	*/
	bool isClose = false;
	_lock.lock();

	/* 记录上一次操作的结果 */
	if(0 == bytesTransfered)
	{
		_ds2Status = DSS_ABORTED;
		_exitCode = flags & IOCP_READTIMEO ? CT_FCGI_RECV_TIMEO : CT_FCGI_RECV_FAILED;
	}
	if(requestEnd)
	{
		/* 尽快释放 FCGI 连接 */
		_ds2Status = DSS_COMPLETE;
		_fcgiFactory->releaseConnection(_fcgiConnection, true);
		_fcgiConnection = NULL;

		/* 接收到所有数据后,把内容的长度写入响应头 */
		if(_cacheAll)
		{
			std::string val("");
			if(_header.getField("Content-Length",val))
			{
				// FCGI 服务器已经指定了 Content-Length 
				assert(atoi(val.c_str()) == _cache->size());
			}
			else
			{
				char tmpSizeBuf[20];
				sprintf(tmpSizeBuf, "%d", _cache->size());
				_header.add("Content-Length", tmpSizeBuf);
				_header.format();
			}
		}
	}

	/*
	* 开始执行一下一个 IO 操作
	*/
	/* 是否继续从 FCGI 服务器接收数据(条件: 数据流2没发生网络错误也没接收到 FCGI_END_REQUEST 并且数据流3没有发生网络错误) */
	if(_ds2Status != DSS_ABORTED && _ds2Status != DSS_COMPLETE && _ds3Status != DSS_ABORTED)
	{
		if(CT_SUCESS != recvFromFCGIServer())
		{
			_ds2Status = DSS_ABORTED;
			_exitCode = CT_FCGI_RECV_FAILED;
		}
	}

	/* 是否需要启动数据流3(条件: 数据流2没有发生网络错误,缓存内有数据,数据流3的状态为暂停 */
	if(_ds2Status != DSS_ABORTED && _ds3Status == DSS_PAUSED)
	{
		if(((_cacheAll && requestEnd) || !_cacheAll) && hasData())
		{
			int sendRes = sendToHTTPClient();
			if(CT_SUCESS != sendRes)
			{
				_ds3Status = DSS_ABORTED;
				_exitCode = sendRes;
			}
			else
			{
				_ds3Status = DSS_RUNING;
			}
		}
	}

	/* 是否退出 */
	isClose = isExitPoint();
	_lock.unlock();

	if(isClose) close(_exitCode);
}

void FCGIResponder::onFCGISend(size_t bytesTransfered, int flags)
{
	if(bytesTransfered == 0)
	{
		close((flags & IOCP_WRITETIMEO) ? CT_FCGI_SEND_TIMEO : CT_FCGI_SEND_FAILED);
		return;
	}

	/*
	* 1. 统计信息
	* 2. 如果缓冲区内还有数据没有一次发完,继续填充缓冲区
	* 3. 如果发送队列中还有record,继续填充缓冲区
	*/
	_bytesFCGISent += bytesTransfered;
	_fcgiSendBuf->seekg(bytesTransfered, SEEK_CUR);

	if( !_fcgiSendBuf->eof() )
	{
		/* 缓冲区内还有数据,继续发送,直到全部发送完毕. */
		int res = sendToFCGIServer();
		if(res != CT_SUCESS) close(res);
	}
	else
	{
		/* 所有数据发送完毕,重置缓冲区 */
		_fcgiSendBuf->trunc(false);

		if(!_request->eof())
		{
			/* 继续发送 POST 数据 */
			int res = sendPostData();
			if( res != CT_SUCESS ) close(res);
		}
		else
		{
			/* 删除数据流1的缓冲区,不再需要了 */
			delete _fcgiSendBuf;
			_fcgiSendBuf = NULL;
			freeBuffer(_postDataBuf);
			_postDataBuf = NULL;

			/* POST 数据已经发送完毕,准备参数接收 FCGI 服务器的响应(数据流2) */
			_fcgiRecvBuf = allocBuffer();
			_httpSendBuf = allocBuffer();
			_stdoutRecord = new FCGIRecord();

			/* 数据流2,3开始运行,此时还不需要做同步控制 */
			_ds2Status = DSS_RUNING;
			_ds3Status = DSS_PAUSED; /* 一开始 cache 内没有数据,所以数据流3的状态为暂停 */
			
			/* 数据流2接收第一段STDOUT数据 */
			int res = recvFromFCGIServer();
			if( res != CT_SUCESS )
			{
				close(res);
			}
		}
	}
}

int FCGIResponder::sendPostData()
{
	/* 确认缓冲区为空 */
	assert(_fcgiSendBuf->tellg() == 0);
	if(_postDataBuf == NULL)
	{
		_postDataBuf = allocBuffer();
	}

	/* 读取一段 POST DATA */
	_postDataBuf->len = _request->read(_postDataBuf->buf, _postDataBuf->size);
	if(_postDataBuf->len > 0)
	{
		/* 把 POST DATA 打包为符合 FCGI 协议的Record 并写入 FCGI Send Buffer */
		FCGIRecordWriter writer(*_fcgiSendBuf);
		writer.writeHeader(_fcgiConnection->requestId, FCGI_STDIN);
		writer.writeBodyData(_postDataBuf->buf, _postDataBuf->len);
		writer.writeEnd();
	}

	/* POST DATA 是否读取完毕 */
	if(_postDataBuf->len < _postDataBuf->size)
	{
		/* POST DATA 读取完毕,追加一个空 STDIN 表示结束 */
		FCGIRecordWriter writer(*_fcgiSendBuf);
		writer.writeHeader(_fcgiConnection->requestId, FCGI_STDIN);
		writer.writeEnd();
	}

	/* 把打包后的数据发送到 FCGI 服务器 */
	if(_fcgiSendBuf->fsize() > 0)
	{
		return sendToFCGIServer();
	}
	else
	{
		/* 会运行到这里来吗? */
		assert(0);
		return CT_UNKNOWN;
	}
}

/*
* 把接收至 FCGI 服务器的数据写入缓存
* 写入之前需要做一些处理
* 1. 把响应头部分写入 _fcgiResponseHeader 直到响应头结束.
* 2. 根据 FCGI 响应头部分的内容生成一个标准的 HTTP 1.1 响应头并写入缓存中.
* 3. 把内容部分写入缓存中.
*/
size_t FCGIResponder::writeToCache(const void *buf, size_t len)
{
	const byte* data = reinterpret_cast<const byte*>(buf);
	size_t bytesWritten = 0;
	size_t bytesParsed = 0;

	if(len == 0)
	{
		/* 写入 chunked 编码的结尾块 */
		if( _chunkCoding && !_chunkEndSent)
		{
			_chunkEndSent = true;

			_lock.lock();
			bytesWritten += _cache->puts("0\r\n\r\n");
			_lock.unlock();
		}
		else
		{
			assert(0);
		}
	}
	else
	{
		/* 缓存由 FCGI 服务进程生成的 HTTP 响应头以备分析 */
		if(!_isFCGIHeaderReceived)
		{
			/* 缓存 HTTP 响应头直到接收到连续两个换行为标志的结尾 */
			while(bytesParsed < len)
			{
				_fcgiResponseHeader.write(data + bytesParsed, 1);
				++bytesParsed;
				if(is_end(reinterpret_cast<const byte*>(_fcgiResponseHeader.buffer()), _fcgiResponseHeader.fsize()))
				{
					_isFCGIHeaderReceived = true;
					break;
				}
			}

			/* 分析由 FCGI 服务进程产生的 HTTP 响应头 */
			if(_isFCGIHeaderReceived)
			{
				/* 生成一个 HTTP 响应头 */
				_svrCode = SC_OK;
				_header.setResponseCode(_svrCode);
				_header.addDefaultFields();
				_header.add(reinterpret_cast<const char*>(_fcgiResponseHeader.buffer()));
				
				/* 分析由 FCGI 进程产生的响应头的几个特殊域: Status, Content-Length, Transfer-Encoding */
				std::string tmp;
				if(_header.getField("Status", tmp))
				{
					// FCGI Status 域指明新的响应码
					_svrCode = atoi(tmp.c_str());
					if( _svrCode == 0) _svrCode = SC_OK;
					_header.setResponseCode(_svrCode);
					_header.remove("Status");
				}

				int contentLen = 0;
				if(_header.getField("Content-Length", tmp))
				{
					// FCGI 进程指明了内容的长度
					contentLen = atoi(tmp.c_str());
				}
				
				if(!_header.getField("Transfer-Encoding", tmp) && contentLen == 0 && !_cacheAll)
				{
					// FCGI 服务器没有指定长度,并且没有指定 Transfer-Encoding 则使用 chunked 编码.
					_chunkCoding = true;
					_header.add("Transfer-Encoding", "chunked");
				}

				/* 是否保持连接 */
				if(_request->keepAlive())
				{
					_header.add("Connection", "keep-alive");
				}
				else
				{
					_header.add("Connection", "close");
				}

				/* 格式化响应头准备输出 */
				if(_cacheAll)
				{
				}
				else
				{
					_header.format();
				}

				///* HTTP 响应头写入缓存 */
				//byte tmpBuf[1024];
				//size_t tmpLen = 0;
				//while((tmpLen = _header.read(tmpBuf, 1024)) != 0)
				//{
				//	bytesWritten += _cache->write(tmpBuf, tmpLen);
				//}
			}
		}

		/* 把其他数据写入缓存 */
		if(bytesParsed < len)
		{
			_lock.lock();

			/* 写分块chunked 编码头: 16进制字符串表示段数据长度 + CRLF */
			if( _chunkCoding )
			{
				char chunkSize[200] = {0};
				sprintf(chunkSize, "%x\r\n", len - bytesParsed);
				bytesWritten += _cache->puts(chunkSize);
			}

			/* 写块数据 */
			bytesWritten += _cache->write(data + bytesParsed, len - bytesParsed);

			/* 块结尾 CRLF */
			if( _chunkCoding )
			{
				bytesWritten += _cache->puts("\r\n");
			}

			_lock.unlock();
		}
	}
	return bytesWritten;
}

/*
* 分析来自 FCGI 服务进程的数据流,按照 FCGI 协议的 record 解析.
*/
bool FCGIResponder::parseStdout(const byte *buf, size_t len)
{
	bool requestEnd = false;
	size_t parsedLen = 0;
	while( parsedLen < len )
	{
		parsedLen += _stdoutRecord->write(buf + parsedLen, len - parsedLen);
		if( _stdoutRecord->check() )
		{
			FCGI_Header header;
			_stdoutRecord->getHeader(header);

			/* 根据 FCGI Record Type 分类处理 */
			if( FCGI_STDOUT == header.type )
			{
				if(_stdoutRecord->getContentLength(header) > 0)
				{
					writeToCache(_stdoutRecord->getBodyData(), _stdoutRecord->getContentLength(header));
				}
				else
				{
					/* 根据FCGI协议,应该有一个长度为0的 FCGI_STDOUT 表示数据结束,从来没收到过...不知道为什么 */
					assert(0);
				}
			}
			else if( FCGI_END_REQUEST == header.type)
			{
				unsigned int appStatus = 0;
				byte protocolStatus = 0;
				if(_stdoutRecord->getEndRequestBody(appStatus, protocolStatus))
				{
					assert(protocolStatus == FCGI_REQUEST_COMPLETE);
					requestEnd = true;
					if(_chunkCoding)
					{
						/* 发送一个结束 chunk */
						writeToCache(NULL, 0);
					}
				}
			}
			else if( FCGI_STDERR == header.type)
			{
				/* 写日志 */
				std::string err;
				err.assign(reinterpret_cast<const char*>(_stdoutRecord->getBodyData()), _stdoutRecord->getBodyLength());
				LOGGER_CWARNING(theLogger, _T("%s\r\n"), AtoT(err).c_str());
			}
			else
			{
				assert(0);
				/* 忽略之 */
			}

			/*
			* 处理下一个record
			*/
			_stdoutRecord->reset();
		}
	}

	return requestEnd;
}

int FCGIResponder::sendHttpError(int errCode, const char *msg)
{	
	assert(_httpErrorCache == NULL);

	char tmp[1024] = {0};
	if(NULL == _httpErrorCache) _httpErrorCache = new memfile();
	if(NULL == _httpSendBuf) _httpSendBuf = allocBuffer();

	/* 生成响应头 */
	_svrCode = errCode;

	_header.setResponseCode(_svrCode);
	_header.addDefaultFields();
	_header.add("Content-Type", "text/plain");
	sprintf(tmp, "%d", strlen(msg));
	_header.add("Content-Length", tmp);
	if(_request->keepAlive())
	{
		_header.add("Connection", "keep-alive");
	}
	else
	{
		_header.add("Connection", "close");
	}
	_header.format();

	/* 把响应头和错误信息写入缓存 */
	size_t rd = 0;
	while( (rd = _header.read(reinterpret_cast<byte*>(tmp), 1024)) > 0)
	{
		_httpErrorCache->write(tmp, rd);
	}
	_httpErrorCache->puts(msg);

	/* 发送第一段数据到 HTTP 客户端 */
	_httpSendBuf->len = _httpErrorCache->read(_httpSendBuf->buf, _httpSendBuf->size);
	if( IOCP_PENDING != _network->send(_clientSock, _httpSendBuf->buf, _httpSendBuf->len, _server->sendTimeout(), IOCPCallback, this))
	{
		return CT_CLIENTCLOSED;
	}
	else
	{
		return CT_SUCESS;
	}
}

void FCGIResponder::close(int exitCode)
{
	if(exitCode != CT_CLIENTCLOSED && _bytesSent == 0 && _httpErrorCache == NULL)
	{
		/* 如果出错时还没有发送任何数据到 HTTP 客户端,则发送一个 500 error 到客户端 */
		int res = sendHttpError(SC_SERVERERROR, g_HTTP_Server_Error);
		if(CT_SUCESS != res) close(res);
	}
	else
	{
		_server->onResponder(this, exitCode);
	}
}

std::string FCGIResponder::getHeader()
{
	return _header.getHeader();
}

bool FCGIResponder::hasData()
{
	return !_header.eof() || !_cache->empty();
}
#include "StdAfx.h"
#include "FCGIFactory.h"
#include "HTTPDef.h"

/*
* FCGI 服务器类型
*/
#define FCGI_SERVER_TYPE_LOCALE 0
#define FCGI_SERVER_TYPE_REMOTE 1

/*
*
*/
#define FCGI_CONNECTION_BUSY 0
#define FCGI_CONNECTION_CONNECTING 1
#define FCGI_CONNECTION_IDLE 2
#define FCGI_CONNECTION_DISCONNECT 3

FCGIFactory::FCGIFactory(IHTTPServer *httpServer, IOCPNetwork *network)
	: _fcgiRequestIdSeed(1), _network(network), _maxConn(200), _fcgiServer(NULL), _httpServer(httpServer),
	_maxWait(SIZE_T_MAX), _hrt(true), _inited(false), _cacheAll(false)
{
}


FCGIFactory::~FCGIFactory()
{
}

int FCGIFactory::init(const std::string &name, unsigned int port, const std::string &fileExts, size_t maxConn, size_t maxWait, bool cacheAll)
{
	_maxConn = maxConn;
	_maxWait = maxWait;
	_cacheAll = cacheAll;

	/* 转换为大写字母统一比较 */
	char exts[MAX_PATH + 1] = {0}; /* 扩展名字符串最多 MAX_PATH 个字符 */
	strcpy(exts, fileExts.c_str());
	strupr(exts);
	_fileExts = exts;

	/* 记录FCGI Server 的信息 */
	_fcgiServer = new fcgi_server_context_t;
	memset(_fcgiServer, 0, sizeof(fcgi_server_context_t));
	strcpy(_fcgiServer->name, name.c_str());

	if(port == 0)
	{
		_fcgiServer->type = FCGI_SERVER_TYPE_LOCALE;
		_fcgiServer->processList = new fcgi_process_list_t;
	}
	else
	{
		_fcgiServer->type = FCGI_SERVER_TYPE_REMOTE;
		_fcgiServer->port = port;
	}

	_inited = true;
	return FCGIF_SUCESS;
}

int FCGIFactory::destroy()
{
	/* 设置退出标志 */
	_lock.lock();
	_inited = false;
	_lock.unlock();

	/* 释放所有连接 */
	while(_workingFcgiConnList.size() > 0)
	{
		//_workingFcgiConnList.front()->comm = IOCP_NULLKEY;
		freeConnectionContext(_workingFcgiConnList.front());
		_workingFcgiConnList.pop_front();
	}
	while( _idleFcgiConnList.size() > 0)
	{
		//_idleFcgiConnList.front()->comm = IOCP_NULLKEY;
		freeConnectionContext(_idleFcgiConnList.front());
		_idleFcgiConnList.pop_front();
	}

	/* 清空等待队列 */
	_waitingList.clear();

	/* 杀死FCGI进程 */
	if(_fcgiServer)
	{
		if(_fcgiServer->processList)
		{
			while(_fcgiServer->processList->size() > 0)
			{
				freeProcessContext(_fcgiServer->processList->front());
				_fcgiServer->processList->pop_front();
			}
			delete _fcgiServer->processList;
		}
		delete _fcgiServer;
		_fcgiServer = NULL;
	}

	/* 重置状态 */
	_fcgiRequestIdSeed = 1;
	return 0;
}

bool FCGIFactory::catchRequest(const std::string &fileName)
{
	/*
	* 目标脚本文件是否存在
	*/
	//if(!WINFile::exist(AtoT(fileName).c_str())) return false;

	std::string exts;
	_lock.lock();
	if(_inited) exts = _fileExts;
	else exts = "";
	_lock.unlock();

	/*
	* 判断url是否符合指定的扩展名
	*/
	std::string ext;
	get_file_ext(fileName, ext);
	char extStr[MAX_PATH] = {0};
	strcpy(extStr, ext.c_str());
	strupr(extStr);
	ext = extStr;

	return match_file_ext(ext, exts);
}

FCGIFactory::fcgi_process_context_t* FCGIFactory::allocProcessContext()
{
	fcgi_process_context_t *context = new fcgi_process_context_t;
	memset(context, 0, sizeof(fcgi_process_context_t));
	context->instPtr = this;
	context->processInfo = new PROCESS_INFORMATION;
	memset(context->processInfo, 0, sizeof(PROCESS_INFORMATION));
	return context;
}

bool FCGIFactory::freeProcessContext(fcgi_process_context_t *context)
{
	/* 连接一定已经先被关闭了 */
	assert(context->conn == NULL);

	/* 如果连接线程正在运行中,等待结束 */
	if(context->thread)
	{
		if( WAIT_OBJECT_0 != WaitForSingleObject(reinterpret_cast<HANDLE>(context->thread), 0) )
		{
			TerminateThread(reinterpret_cast<HANDLE>(context->thread), 1);
		}
		CloseHandle(reinterpret_cast<HANDLE>(context->thread));
	}

	/* 如果FCGI进程正在运行,终止之 */
	if(context->processInfo->hProcess)
	{
		if(WAIT_OBJECT_0 != WaitForSingleObject(context->processInfo->hProcess, 0))
		{
			TerminateProcess(context->processInfo->hProcess, 1);
		}
		CloseHandle(context->processInfo->hThread);
		CloseHandle(context->processInfo->hProcess);
	}
	delete context->processInfo;
	delete context;
	return true;
}

fcgi_conn_t* FCGIFactory::allocConnectionContext()
{
	fcgi_conn_t *conn = new fcgi_conn_t;
	memset(conn, 0, sizeof(fcgi_conn_t));

	conn->requestId = _fcgiRequestIdSeed++;
	if(conn->requestId == 0) conn->requestId = 1;
	conn->cacheAll = _cacheAll;
	conn->instPtr = this;

	return conn;
}

FCGIFactory::fcgi_process_context_t* FCGIFactory::getProcessContext(fcgi_conn_t *conn)
{
	fcgi_process_context_t *procContext = NULL;
	if(_fcgiServer->processList)
	{
		for(fcgi_process_list_t::iterator iter = _fcgiServer->processList->begin(); iter != _fcgiServer->processList->end(); ++iter)
		{
			if( conn == (*iter)->conn )
			{
				procContext = *iter;
				break;
			}
		}
	}
	return procContext;
}

bool FCGIFactory::freeConnectionContext(fcgi_conn_t *conn)
{
	if(conn->comm != IOCP_NULLKEY)
	{
		_network->remove(conn->comm);
	}
	delete conn;

	/*
	* 对于本地模式,每个进程都由对应的一个连接.
	*/
	fcgi_process_context_t *procContext = getProcessContext(conn);
	if(procContext) procContext->conn = NULL;

	return true;
}

void FCGIFactory::onConnect(fcgi_conn_t *conn, bool sucess)
{
	//TRACE("fcgi connection: 0x%x %s.\r\n", conn, sucess ? "connected" : "unconnect"); 
	fcgi_conn_ready_func_t waitingFunc = NULL;
	void *waitingParam = NULL;

	_lock.lock();
	
	/* 取出等待队列的队头请求 */
	if(_waitingList.size() > 0)
	{
		waitingFunc = _waitingList.front().first;
		waitingParam = _waitingList.front().second;
		_waitingList.pop_front();
	}

	if(!sucess)
	{
		/* 连接失败,从忙碌队列中移除(连接前conn已经被放入忙碌队列) */
		_workingFcgiConnList.remove(conn);

		/* 释放连接 */
		freeConnectionContext(conn);
		conn = NULL;
	}
	else
	{
		/* 连接成功,保留conn在忙碌队列中(连接前conn已经被放入忙碌队列) */
		/* 如果没有等待中的请求,则放入空闲队列 */
		if(waitingFunc)
		{
			/* 依然保留在忙碌队列中 */
		}
		else
		{
			_workingFcgiConnList.remove(conn);
			_idleFcgiConnList.push_back(conn);
		}
	}
	_lock.unlock();

	if(waitingFunc)
	{
		waitingFunc(conn, waitingParam);
	}
}

void FCGIFactory::IOCPCallback(iocp_key_t s, int flags, bool result, int transfered, byte* buf, size_t len, void* param)
{
	assert(flags & IOCP_CONNECT);
	fcgi_conn_t *conn = reinterpret_cast<fcgi_conn_t*>(param);
	FCGIFactory *instPtr = reinterpret_cast<FCGIFactory*>(conn->instPtr);
	instPtr->onConnect(conn, result);
}

bool FCGIFactory::getConnection(fcgi_conn_t *&connOut, fcgi_conn_ready_func_t callbackFunc, void *param)
{
	assert(_fcgiServer);
	fcgi_conn_t *conn = NULL;
	bool sucess = false;
	connOut = NULL;
	fcgi_conn_ready_func_t expiredFunc = NULL;
	void *expiredParam = NULL;

	_lock.lock();
	do
	{
		if(!_inited) break;

		/* 尝试从空闲连接队列中分配 */
		if(_idleFcgiConnList.size() > 0)
		{
			conn = _idleFcgiConnList.front();
			_idleFcgiConnList.pop_front();
			_workingFcgiConnList.push_back(conn);
			sucess = true;
			connOut = conn;
			break;
		}

		/* 如果没有达到最大连接数,则新创建一个连接 */
		if(_workingFcgiConnList.size() < _maxConn)
		{
			/* 分配一个新连接,并且进入忙碌队列 */
			/* 把获取连接请求放入等待队列,一旦新连接初始化完成则回调 */
			conn = allocConnectionContext(); 
			_workingFcgiConnList.push_back(conn);
			_waitingList.push_back(std::make_pair(callbackFunc, param));
			
			//TRACE("fcgi connections: 0x%x created.\r\n", conn);

			/* 初始化该连接 */
			if(initConnection(conn))
			{
				/* 新建连接已经成功,正在连接,等待回调函数通知 */
				sucess = true;
			}
			else
			{
				/* 建立连接失败,回收连接,恢复现场 */
				assert(0);
				_waitingList.pop_back();
				_workingFcgiConnList.pop_back();
				freeConnectionContext(conn);
			}

			break;
		}

		/* 如果已经达到最大连接数,进入等待队列 */
		if(_waitingList.size() >= _maxWait)
		{
			/* 如果等待队列达到最大限制,则弹出队头记录,回调通知超时,然后把当前请求加入到队尾 */
			expiredFunc = _waitingList.front().first;
			expiredParam = _waitingList.front().second;

			_waitingList.pop_front();
		}
		_waitingList.push_back(std::make_pair(callbackFunc, param));
		sucess = true;
		
	}while(0);
	_lock.unlock();

	/* 被挤出的请求等待-失败通知 */
	if(expiredFunc)
	{
		expiredFunc(NULL, expiredParam);
	}

	return sucess;
}

void FCGIFactory::releaseConnection(fcgi_conn_t* conn, bool good)
{
	fcgi_conn_ready_func_t waitingFunc = NULL;
	void *waitingParam = NULL;

	_lock.lock();
	do
	{
		if(!_inited) break;

		/* 如果连接已损坏,则关闭之 */
		if(!good)
		{
			_workingFcgiConnList.remove(conn);
			freeConnectionContext(conn);
			conn = NULL;
		}
	
		/* 检查等待队列 */
		if( _waitingList.size() > 0)
		{
			if(!conn)
			{
				/* 已经有空间,足够创建一个新连接 */
				conn = allocConnectionContext(); 
				_workingFcgiConnList.push_back(conn);

				/* 初始化该连接 */
				if(initConnection(conn))
				{
					/* 新建连接已经成功,正在连接,等待回调函数通知 */
				}
				else
				{
					assert(0);
					_workingFcgiConnList.pop_back();
					freeConnectionContext(conn);
					conn = NULL;
				}
			}
			else
			{
				/* 把当前连接(完好的)分配给等待队列队头的请求,conn依然保留在忙碌队列中 */
				waitingFunc = _waitingList.front().first;
				waitingParam = _waitingList.front().second;
				_waitingList.pop_front();
			}
		}
		else
		{
			/* 把依然可用的连接放入空闲队列中 */
			if(conn)
			{
				_workingFcgiConnList.remove(conn);

				conn->idleTime = _hrt.now();
				_idleFcgiConnList.push_back(conn);

				//TRACE("fcgi connection: 0x%x becomes idle.\r\n", conn);
			}
		}
		
		/* 维护空闲队列一次 */
		maintain();

	}while(0);
	_lock.unlock();

	/*
	* 通知等待队列中的队头请求
	*/
	if(waitingFunc)
	{
		waitingFunc(conn, waitingParam);
	}
}

/*
* 由于没有使用定时器,事实上不能保证能尽快释放多余的空闲连接,需要有新的请求被释放时才触发 maintain()
* 但是从一个较长运行周期来看,可以达到目的.
*/
void FCGIFactory::maintain()
{
	/*
	* 关闭所有空闲超过 FCGI_MAX_IDLE_SECONDS 的连接,空闲队列内至少还有一个连接(就是刚刚被释放的那个)
	*/
	if(_idleFcgiConnList.size() > 1)
	{
		for(fcgi_conn_list_t::iterator iter = _idleFcgiConnList.begin(); iter != _idleFcgiConnList.end(); )
		{
			fcgi_conn_t *conn = *iter;
			if(_hrt.getMs(_hrt.now() - conn->idleTime) >= FCGI_MAX_IDLE_SECONDS * 1000)
			{
				/* 连接对应的本地进程 */
				fcgi_process_context_t *procContext = getProcessContext(conn);

				/* 关闭连接 */
				_idleFcgiConnList.erase(iter++);
				freeConnectionContext(conn);

				/* 把连接对应的本地 FCGI 进程杀死 */
				if(procContext)
				{
					_fcgiServer->processList->remove(procContext);
					freeProcessContext(procContext);
				}
			}
			else
			{
				++iter;
			}
		}
	}
}

bool FCGIFactory::initConnection(fcgi_conn_t *conn)
{
	assert(_fcgiServer);
	if(_fcgiServer->type == FCGI_SERVER_TYPE_REMOTE)
	{
		/* 创建套接字 */
		SOCKET s = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
		if( INVALID_SOCKET == s )
		{
			assert(0);
			LOGGER_CERROR(theLogger, _T("无法为FCGI连接创建套接字,错误码[%d].\r\n"), WSAGetLastError());
			return false;
		}
		else
		{
			/* 绑定套接字到本地的任意一个端口 */
			sockaddr_in localAddr;
			memset(&localAddr, 0, sizeof(sockaddr_in));
			localAddr.sin_port = 0;
			localAddr.sin_family = AF_INET;
			localAddr.sin_addr.s_addr = INADDR_ANY;
			if(SOCKET_ERROR == bind(s, reinterpret_cast<sockaddr*>(&localAddr), sizeof(sockaddr_in)))
			{
				closesocket(s);
				assert(0);
				LOGGER_CERROR(theLogger, _T("无法为FCGI连接套接字绑定本地端口,错误码[%d].\r\n"), WSAGetLastError());
				return false;
			}

			/* 添加到网络模块,没有会话超时也不限制速度 */
			conn->comm = _network->add(s, 0, 0, 0);

			/* 连接到FCGI服务器 */
			sockaddr_in fcgiServerAddr;
			memset(&fcgiServerAddr, 0, sizeof(sockaddr_in));
			fcgiServerAddr.sin_port = htons(_fcgiServer->port);
			fcgiServerAddr.sin_family = AF_INET;
			fcgiServerAddr.sin_addr.s_addr = inet_addr(_fcgiServer->name);

			if( IOCP_PENDING == _network->connect(conn->comm, reinterpret_cast<sockaddr*>(&fcgiServerAddr), NULL, 0, _httpServer->sendTimeout(), IOCPCallback, conn))
			{
				return true;
			}
			else
			{
				LOGGER_CERROR(theLogger, _T("无法连接到FCGI服务器[%s:%d],错误码[%d].\r\n"), AtoT(_fcgiServer->name).c_str(), _fcgiServer->port, _network->getLastError());
				return false;
			}
		}
	}
	else
	{
		/*
		* 为该连接分配一个空闲的 FCGI 本地进程,如果没有空闲进程,则创建一个
		*/
		fcgi_process_context_t *context = NULL;
		for(fcgi_process_list_t::iterator iter = _fcgiServer->processList->begin(); iter != _fcgiServer->processList->end(); ++iter)
		{
			if( (*iter)->conn == NULL )
			{
				context = (*iter);
				break;
			}
		}
		if(!context)
		{
			/* 需要新创建一个进程 */
			assert(_fcgiServer->processList->size() < _maxConn);
			context = allocProcessContext();
			_fcgiServer->processList->push_back(context);
		}
		else
		{
			/* 不需要创建新进程,应该把上一次连接时的线程句柄关闭,以便新创建一个线程 */
			assert(context->thread);
			CloseHandle(reinterpret_cast<HANDLE>(context->thread));
			context->thread = NULL;
		}
		context->conn = conn;

		/*
		* 创建一个线程,在线程函数中执行以下操作:
		* 1. 生成唯一的命名管道文件名(考虑多个 HTTP server 实例,应确保整个操作系统内(而不是仅仅进程内)的唯一性.
		* 2. 创建一个FCGI本地进程.
		* 3. 用命名管道连接到新创建的FCGI本地进程.
		* 4. 回调 FCGIFactory 通知结果.
		*/
		context->thread = _beginthreadex(NULL, 0, spawnChild, context, 0, NULL);
		if(context->thread == -1)
		{
			context->thread = 0;
			LOGGER_CERROR(theLogger, _T("无法为创建FCGI本地进程生成一个工作线程,_beginthreadex调用失败,错误码:%d.\r\n"), errno);
			
			_fcgiServer->processList->remove(context);
			freeProcessContext(context);
			return false;
		}
		else
		{
			return true;
		}
	}
}

unsigned __stdcall FCGIFactory::spawnChild(void *param)
{
	fcgi_process_context_t *context = reinterpret_cast<fcgi_process_context_t*>(param);
	bool sucess = false;

	do
	{
		/* 判断是否需要新创建一个进程: 1.进程句柄为NULL,表示新分配; 2.进程句柄不为空,且有信号,表示进程已经退出了,也需要重新创建 */
		if( context->processInfo->hProcess != NULL )
		{
			if(WAIT_OBJECT_0 == WaitForSingleObject(context->processInfo->hProcess, 0))
			{
				CloseHandle(context->processInfo->hThread);
				CloseHandle(context->processInfo->hProcess);
				memset(context->processInfo, 0, sizeof(PROCESS_INFORMATION));
			}
		}
		if(  context->processInfo->hProcess == NULL )
		{
			/* 生成一个唯一的管道名 */
			unsigned int seed = static_cast<unsigned int>(time( NULL )); /* 确保不同exe实例间不同 */
			seed += reinterpret_cast<unsigned int>(context); /* 确保同一个exe实例中,不同的线程不同 */
			srand(seed);
			sprintf(context->pipeName, "%s\\%04d_%04d", FCGI_PIPE_BASENAME, rand() % 10000, rand() % 10000);
			TRACE("pipename:%s.\r\n", context->pipeName);

			/* 创建一个命名管道 */
			HANDLE hPipe = CreateNamedPipe(AtoT(context->pipeName).c_str(),  PIPE_ACCESS_DUPLEX,
				PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_READMODE_BYTE,
				PIPE_UNLIMITED_INSTANCES,
				4096, 4096, 0, NULL);
			if( INVALID_HANDLE_VALUE == hPipe )
			{
				LOGGER_CERROR(theLogger, _T("无法创建命名管道,本地FCGI进程生成失败,错误码:%d\r\n"), GetLastError());
				break;
			}
			if(!SetHandleInformation(hPipe, HANDLE_FLAG_INHERIT, TRUE))
			{
				LOGGER_CERROR(theLogger, _T("SetHandleInformation()调用失败,本地FCGI进程生成失败,错误码:%d\r\n"), GetLastError());
				break;
			}

			/* 以命名管道为STDIN创建一个本地FCGI进程 */
			STARTUPINFO startupInfo;
			memset(&startupInfo, 0, sizeof(STARTUPINFO));
			startupInfo.cb = sizeof(STARTUPINFO);
			startupInfo.dwFlags = STARTF_USESTDHANDLES;
			startupInfo.hStdInput  = hPipe;
			startupInfo.hStdOutput = INVALID_HANDLE_VALUE;
			startupInfo.hStdError  = INVALID_HANDLE_VALUE;

			if( !CreateProcess(AtoT(context->instPtr->_fcgiServer->name).c_str(), NULL, NULL, NULL, TRUE,

#ifdef _DEBUG /* 调试状态下,创建的PHP-CGI进程带控制台窗口, release时不带控制台窗口 */	
				0, 
#else
				CREATE_NO_WINDOW,
#endif

				NULL, NULL, &startupInfo, context->processInfo))
			{
				LOGGER_CERROR(theLogger, _T("CreateProcess()调用失败,无法生成本地FCGI进程,错误:%s.\r\n"), AtoT(get_last_error()).c_str());
				break;
			}
		}

		/* 等待命名管道 */
		if(!WaitNamedPipe(AtoT(context->pipeName).c_str(), FCGI_CONNECT_TIMEO))
		{
			LOGGER_CERROR(theLogger, _T("连接命名管道失败[%s]\r\n"), AtoT(get_last_error()).c_str());
			break;
		}

		/* 创建一个文件句柄,连接命名管道 */
		HANDLE hConn = CreateFile(AtoT(context->pipeName).c_str(),
			GENERIC_WRITE | GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL,
			OPEN_EXISTING,
			FILE_FLAG_OVERLAPPED,
			NULL);
		if( INVALID_HANDLE_VALUE == hConn )
		{
			LOGGER_CERROR(theLogger, _T("CreateFile()调用失败,无法连接命名管道,错误码:%d\r\n"), GetLastError());
			break;
		}

		/* 命名管道连接成功 */
		context->conn->comm = context->instPtr->_network->add(hConn, 0, 0, 0);
		sucess = true;

	}while(0);

	/* 回调结果 */
	context->instPtr->onConnect(context->conn, sucess);

	return 0;
}

#include "StdAfx.h"
#include <assert.h>
#include "IOCPNetwork.h"

IOCPNetwork::IOCPNetwork()
	: _iocpHandle(NULL),
	_threads(0),
	_tids(NULL),
	_lastErrorCode(0),
	_inited(false),
	_hrt(true)
{
}

IOCPNetwork::~IOCPNetwork()
{
	assert(_inited == false);
}

bool IOCPNetwork::initWinsockLib(WORD nMainVer, WORD nSubVer)
{
	WORD wVer;
	WSADATA ws;
	wVer = MAKEWORD(nMainVer, nSubVer);
	return WSAStartup(wVer, &ws) == 0;
}

bool IOCPNetwork::cleanWinsockLib()
{
	return WSACleanup() == 0;
}

int IOCPNetwork::init(int threads)
{
	/*
	* 保存参数
	* 如果不能获得处理器个数,默认创建5个线程
	*/

	if(threads <= 0)
	{
		SYSTEM_INFO sysInfo;
		GetSystemInfo(&sysInfo);
		threads = sysInfo.dwNumberOfProcessors;
	}
	if(threads <= 0 || threads > 64) threads = 5; 
	
	/*
	* 创建完成端口
	*/
	assert(_iocpHandle == NULL);
	if( NULL == (_iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, threads)) )
	{
		_lastErrorCode = GetLastError();
		destroy();
		return IOCP_UNDEFINED;
	}

	/*
	* 初始化定时器队列
	*/
	_timerQueue.init();

	/*
	* 创建同步对象池
	*/
	_lockPool.init(0);

	/*
	* 创建工作线程
	*/
	_threads = threads;
	assert(_tids == NULL);
	if(_tids) delete []_tids;
	_tids = new uintptr_t[threads];
	assert(_tids);
	memset(_tids, 0, sizeof(uintptr_t) * threads);

	for(int i = 0; i < _threads; ++i)
	{
		if(0 == (_tids[i] = _beginthreadex(NULL, 0, serviceProc, this, 0, NULL)))
		{
			_lastErrorCode = errno;
			destroy();
			return IOCP_UNDEFINED;
		}
	}

	_inited = true;
	return IOCP_SUCESS;
}

int IOCPNetwork::destroy()
{
	/*
	* destroy 需要等待工作线程结束,如果工作线程中调用destroy,会死锁.
	*/
	if(_tids)
	{
		uintptr_t curThread = reinterpret_cast<uintptr_t>(GetCurrentThread());
		for(int i = 0; i < _threads; ++i)
		{
			if(curThread == _tids[i]) return IOCP_DESTROYFAILED;
		}
	}

	/* 
	* 马上设置停止标志 
	*/
	_inited = false;

	/*
	* 发送退出通知,确保工作线程都从GetQueuedCompletionStatus阻塞中返回并终止运行.
	*/
	if(_tids)
	{
		assert(_threads > 0);
		if(_iocpHandle)
		{
			for(int i = 0; i < _threads; ++i)
			{
				PostQueuedCompletionStatus(_iocpHandle, 0, NULL, NULL);
			}
		}

		for(int i = 0; i < _threads; ++i)
		{
			if( _tids[i] ) 
			{
				WaitForSingleObject(reinterpret_cast<HANDLE>(_tids[i]), INFINITE);
				CloseHandle(reinterpret_cast<HANDLE>(_tids[i])); 
			}
		}
		delete []_tids;
	}
	_tids = NULL;
	_threads = 0;

	/*
	* 关闭完成端口句柄
	*/
	if(_iocpHandle)
	{
		if(!CloseHandle(_iocpHandle))
		{
			assert(0);
		}
		_iocpHandle = NULL;
	}

	/*
	* 关闭定时器
	*/
	_timerQueue.destroy();

	/*
	* 回收资源
	*/
	if(_contextMap.size() > 0)
	{
		for(context_map_t::iterator iter = _contextMap.begin(); iter != _contextMap.end(); ++iter)
		{
			iocp_context_t *context = iter->second;

			/*
			* 关闭套接字
			*/
			if(!(context->status & IOCP_HANDLE_CLOSED))
			{
				closeHandle(context);
				context->status |= IOCP_HANDLE_CLOSED;
			}
			freeContext(context);
		}
		_contextMap.clear();
	}

	/*
	* 同步锁池
	*/
	_lockPool.destroy();

	return IOCP_SUCESS;
}

IOCPNetwork::iocp_context_t* IOCPNetwork::allocContext()
{
	iocp_context_t* context = new iocp_context_t;
	memset(context, 0, sizeof(iocp_context_t));
	context->instPtr = this;
	return context;
}

void IOCPNetwork::freeContext(iocp_context_t* context)
{
	delete context;
}

void IOCPNetwork::cleanOlp(iocp_overlapped_t* olp)
{
	/*
	* 重新设置重叠结构的某些值.
	*/
	olp->oppType = IOCP_NONE;
	if(olp->timer != NULL)
	{
		_timerQueue.deleteTimer(olp->timer, false);
		olp->timer = NULL;
	}
	memset(&olp->olp, 0, sizeof(OVERLAPPED));
	olp->buf = NULL;
	olp->len = 0;
	olp->realLen = 0;
	olp->param = NULL;
}

void IOCPNetwork::closeHandle(iocp_context_t *context)
{
	if(context->type == IOCP_HANDLE_SOCKET)
	{
		SOCKET s = reinterpret_cast<SOCKET>(context->h);
		shutdown(s, SD_BOTH);
		closesocket(s);
	}
	else
	{
		CloseHandle(context->h);
	}
}

bool IOCPNetwork::sessionTimeout(iocp_context_t* context)
{
	if(context->sessionTimeo == 0)
	{
		return false;
	}
	else
	{
		return _hrt.getMs(_hrt.now() - context->startCounter) >= context->sessionTimeo;
	}
}

iocp_key_t IOCPNetwork::add(HANDLE f, unsigned long sessionTimeo, size_t readSpeedLmt, size_t writeSpeedLmt, bool sync)
{
	return add(f, sessionTimeo, readSpeedLmt, writeSpeedLmt, true, sync);
}

iocp_key_t IOCPNetwork::add(SOCKET s, unsigned long sessionTimeo, size_t readSpeedLmt, size_t writeSpeedLmt, bool sync)
{
	return add(reinterpret_cast<HANDLE>(s), sessionTimeo, readSpeedLmt, writeSpeedLmt, false, sync);
}

iocp_key_t IOCPNetwork::add(HANDLE h, unsigned long sessionTimeo, size_t readSpeedLmt, size_t writeSpeedLmt, bool isFile, bool sync)
{
	iocp_context_t* context = allocContext();

	context->h = h;
	if(isFile) context->type = IOCP_HANDLE_FILE;
	else context->type = IOCP_HANDLE_SOCKET;
	context->lockPtr = NULL;
	context->readOlp.speedLmt = readSpeedLmt;
	context->writeOlp.speedLmt = writeSpeedLmt;
	context->startCounter = _hrt.now();
	context->sessionTimeo = sessionTimeo;

	bool ret = true;
	_lock.lock();
	do
	{
		if( _iocpHandle != CreateIoCompletionPort(h, _iocpHandle, reinterpret_cast<ULONG_PTR>(context), 0))
		{
			assert(0);
			ret = false;
			break;
		}
		else
		{
			if(sync)
			{
				context->lockPtr = _lockPool.allocate();
			}
			else
			{
				context->lockPtr = NULL;
			}
			_contextMap.insert(std::make_pair(h, context));
		}
	}while(0);
	_lock.unlock();

	if(ret)
	{
		//TRACE("iocp key 0x%x allocated.\r\n", context);
		return context;
	}
	else
	{
		freeContext(context);
		return IOCP_NULLKEY;
	}
}

SOCKET IOCPNetwork::getSocket(iocp_key_t key)
{
	iocp_context_t* context = reinterpret_cast<iocp_context_t*>(key);
	assert(context->type == IOCP_HANDLE_SOCKET);
	return reinterpret_cast<SOCKET>(context->h);
}

//iocp_key_t IOCPNetwork::getKey(SOCKET s)
//{
//	iocp_key_t ret = IOCP_NULLKEY;
//	_lock.lock();
//	context_map_t::iterator iter = _contextMap.find(s);
//	if(iter != _contextMap.end())
//	{
//		ret = iter->second;
//	}
//	_lock.unlock();
//	return ret;
//}

bool IOCPNetwork::refresh(iocp_key_t key)
{
	iocp_context_t* context = reinterpret_cast<iocp_context_t*>(key);
	bool ret = false;

	if(context->lockPtr) context->lockPtr->wlock();
	if(context->readOlp.oppType != IOCP_NONE || context->writeOlp.oppType != IOCP_NONE || context->status != IOCP_NORMAL)
	{
		/* 状态正常,且没有 IO 操作正在进行时才允许刷新 */
	}
	else
	{
		context->startCounter = _hrt.now();
		ret = true;
	}
	if(context->lockPtr) context->lockPtr->unlock();

	return ret;
}

bool IOCPNetwork::busy(iocp_key_t key)
{
	iocp_context_t* context = reinterpret_cast<iocp_context_t*>(key);
	bool ret = false;

	if(context->lockPtr) context->lockPtr->wlock();
	ret = context->readOlp.oppType != IOCP_NONE || context->writeOlp.oppType != IOCP_NONE;
	if(context->lockPtr) context->lockPtr->unlock();

	return ret;
}

int IOCPNetwork::cancel(iocp_key_t key)
{
	iocp_context_t* context = reinterpret_cast<iocp_context_t*>(key);
	int ret = IOCP_SUCESS;

	/*
	* 如果有速度限制定时器,则先删除定时器.
	*/
	if(context->lockPtr) context->lockPtr->wlock();

	if(context->readOlp.oppType == IOCP_DELAY_READ)
	{
		context->readOlp.oppType = IOCP_NONE;
		_timerQueue.deleteTimer(context->readOlp.timer, false);
		context->readOlp.timer = NULL;
	}
	if(context->writeOlp.oppType == IOCP_DELAY_WRITE)
	{
		context->writeOlp.oppType = IOCP_NONE;
		_timerQueue.deleteTimer(context->writeOlp.timer, false);
		context->writeOlp.timer = NULL;
	}

	/* 
	* 如果此时有异步操作正在进行,则关闭套接字使异步操作失败并返回. 
	*/
	if(context->status == IOCP_NORMAL && (context->readOlp.oppType != IOCP_NONE || context->writeOlp.oppType != IOCP_NONE))
	{
		closeHandle(context);
		context->status |= (IOCP_CANCELED | IOCP_HANDLE_CLOSED);
		ret = IOCP_PENDING;
	}
	if(context->lockPtr) context->lockPtr->unlock();

	return ret;
}

/*
* 将套接字句柄从IOCP模块中移除,并且套接字句柄将被关闭.
* 只有在异步操作都完成后才能移除.
*/
//
int IOCPNetwork::remove(iocp_key_t key)
{
	if( !_inited ) return IOCP_UNINITIALIZED;
	iocp_context_t* context = reinterpret_cast<iocp_context_t*>(key);
	bool isBusy = false;
	
	/*
	* 如果套接字处于忙碌状态,则设置删除标志并关闭套接字.
	*/
	if(context->lockPtr) context->lockPtr->wlock();
	if( context->readOlp.oppType != IOCP_NONE || context->writeOlp.oppType != IOCP_NONE )
	{
		isBusy = true;
	}
	else
	{
		/* 空闲状态,设置一个删除标志,禁止继续操作 */
		context->status |= IOCP_REMOVE;
	}
	if(context->lockPtr) context->lockPtr->unlock();

	/*
	* 如果套接字处于忙碌状态,则关闭
	*/
	if(isBusy)
	{
		return IOCP_BUSY;
	}
	
	/*
	* 删除各个定时器.延时操作.
	*/
	if(context->readOlp.timer != NULL)
	{
		assert(0);
		_timerQueue.deleteTimer(context->readOlp.timer, true);
		context->readOlp.timer = NULL;
	}
	if(context->writeOlp.timer != NULL)
	{
		assert(0);
		_timerQueue.deleteTimer(context->writeOlp.timer, true);
		context->writeOlp.timer = NULL;
	}

	/*
	* 关闭套接字
	*/
	if(!(context->status & IOCP_HANDLE_CLOSED))
	{
		closeHandle(context);
		context->status |= IOCP_HANDLE_CLOSED;
	}

	/*
	* 移除记录
	*/
	_lock.lock();
	_contextMap.erase(context->h);
	if(context->lockPtr)
	{
		_lockPool.recycle(context->lockPtr);
		context->lockPtr = NULL;
	}
	_lock.unlock();

	/*
	* 回收资源
	*/
	freeContext(context);
	return IOCP_SUCESS;
}

void IOCPNetwork::readTimeoutProc(void* param, unsigned char)
{
	iocp_context_t* context = reinterpret_cast<iocp_context_t*>(param);

	if(context->lockPtr) context->lockPtr->wlock();
	if(context->readOlp.oppType != IOCP_NONE && context->status == IOCP_NORMAL)
	{
		context->instPtr->closeHandle(context);
		context->status |= (IOCP_READTIMEO | IOCP_HANDLE_CLOSED);
	}
	if(context->lockPtr) context->lockPtr->unlock();
}

void IOCPNetwork::writeTimeoutProc(void* param, unsigned char)
{
	iocp_context_t* context = reinterpret_cast<iocp_context_t*>(param);

	if(context->lockPtr) context->lockPtr->wlock();
	if(context->writeOlp.oppType != IOCP_NONE && context->status == IOCP_NORMAL)
	{
		context->instPtr->closeHandle(context);
		context->status |= (IOCP_WRITETIMEO | IOCP_HANDLE_CLOSED);
	}
	if(context->lockPtr) context->lockPtr->unlock();
}

void IOCPNetwork::delaySendProc(void* param, unsigned char)
{
	iocp_context_t* context = reinterpret_cast<iocp_context_t*>(param);
	iocp_proc_t func = NULL;
	void* callbackParam = NULL;
	int flags = IOCP_NONE;
	byte* buf = NULL;
	size_t len = 0;
	int ret = IOCP_PENDING;

	/*
	* 删除延时操作的定时器.
	*/
	assert(context->writeOlp.timer);
	if(context->writeOlp.timer)
	{
		/* 回调函数内不应该等待定时器,否则会死锁 */
		context->instPtr->_timerQueue.deleteTimer(context->writeOlp.timer, false);
		context->writeOlp.timer = NULL;
	}

	if(context->lockPtr) context->lockPtr->rlock();
	if(context->status != IOCP_NORMAL)
	{
		ret = IOCP_BUSY;
	}
	else
	{
		if(context->writeOlp.oppType != IOCP_DELAY_WRITE)
		{
			/* 延时发送定时器被取消时,定时器函数正在等待进入临界段 */
		}
		else
		{
			context->writeOlp.oppType = IOCP_SEND;
			if(context->writeOlp.timeout > 0)
			{
				context->writeOlp.timer = context->instPtr->_timerQueue.createTimer(context->writeOlp.timeout, writeTimeoutProc, context);
			}

			ret =  context->instPtr->realSend(context);

			/*
			* 如果接收操作失败,应该恢复现场,以便下一次调用可以成功.
			*/
			if(IOCP_PENDING != ret)
			{
				func = context->writeOlp.iocpProc;
				callbackParam = context->writeOlp.param;
				flags = context->writeOlp.oppType;
				buf = context->writeOlp.buf;
				len = context->writeOlp.len;

				context->instPtr->cleanOlp(&context->writeOlp);
			}
		}
	}
	if(context->lockPtr) context->lockPtr->unlock();


	if(func)
	{
		/* 模拟一个失败的结果 */
		/* 如果回调函数阻塞,则会影响定时器的准确性*/
		func(context, flags, false, 0, buf, len, callbackParam);
	}
}

void IOCPNetwork::delayRecvProc(void* param, unsigned char)
{
	iocp_context_t* context = reinterpret_cast<iocp_context_t*>(param);
	
	iocp_proc_t func = NULL;
	void* callbackParam = NULL;
	int flags = IOCP_NONE;
	byte* buf = NULL;
	size_t len = 0;
	int ret = IOCP_PENDING;

	/*
	* 删除延时操作的定时器.
	* 如果使用的 Windows TimerQueue,则不能在此处删除定时器,因为在定时器回调函数中删除定时器是不能成功的.
	*/
	assert(context->readOlp.timer);
	if(context->readOlp.timer)
	{
		/* 回调函数内不应该等待定时器,否则会死锁 */
		context->instPtr->_timerQueue.deleteTimer(context->readOlp.timer, false);
		context->readOlp.timer = NULL;
	}

	if(context->lockPtr) context->lockPtr->rlock();
	if(context->status != IOCP_NORMAL)
	{
		ret = IOCP_BUSY;
	}
	else
	{
		if(context->readOlp.oppType != IOCP_DELAY_READ)
		{
		}
		else
		{
			context->readOlp.oppType = IOCP_RECV;
			
			/*
			* 创建超时定时器
			*/
			if(context->readOlp.timeout > 0)
			{
				context->readOlp.timer = context->instPtr->_timerQueue.createTimer(context->readOlp.timeout, readTimeoutProc, context);
			}

			int ret =  context->instPtr->realRecv(context);

			/*
			* 如果接收操作失败,应该恢复现场,以便下一次调用可以成功.
			*/
			if(IOCP_PENDING != ret)
			{
				func = context->readOlp.iocpProc;
				callbackParam = context->readOlp.param;
				flags = context->readOlp.oppType;
				buf = context->readOlp.buf;
				len = context->readOlp.len;

				context->instPtr->cleanOlp(&context->readOlp);
			}
		}
	}
	if(context->lockPtr) context->lockPtr->unlock();


	if(func)
	{
		/* 模拟一个失败的结果 */
		/* 如果回调函数阻塞,则会影响定时器的准确性*/
		func(context, flags, false, 0, buf, len, callbackParam);
	}
}

unsigned int __stdcall IOCPNetwork::serviceProc(void* lpParam)
{
	IOCPNetwork* instPtr = reinterpret_cast<IOCPNetwork*>(lpParam);

	while(1)
	{
		DWORD transfered = 0;
		iocp_context_t* context = NULL;
		iocp_overlapped_t* iocpOlpPtr = NULL;
		if(!GetQueuedCompletionStatus(instPtr->_iocpHandle, &transfered, reinterpret_cast<PULONG_PTR>(&context), reinterpret_cast<LPOVERLAPPED*>(&iocpOlpPtr), INFINITE))
		{
			if(iocpOlpPtr)
			{
				/*
				* 成功从IOCP队列中取得一个包,但是I/O操作被标记为失败.
				* 原因1: 远程主机关闭了连接.
				* 原因2: 由于超时,本地套接字被 shutdown.
				* 原因3: 本程序不出现. (由于调用了 CancelIoEx 导致异步操作被取消. GetLastError() == ERROR_OPERATION_ABORTED)
				* 
				*/
				//assert(0);
				instPtr->onIoFinished(context, false, iocpOlpPtr, transfered);
			}
			else
			{
				/*
				* IOCP句柄被关闭.
				* 在本程序逻辑中属于非正常退出.
				*/
				assert(0);
				instPtr->_lastErrorCode = GetLastError();
				return IOCP_UNDEFINED;
			}
		}
		else
		{
			/*
			* 约定的正常退出标志
			*/
			if(transfered == 0 && iocpOlpPtr == NULL && context == NULL)
			{
				TRACE("IOCPNetwork::serviceProc()IOCP工作线程退出.\r\n");
				break;
			}
			
			/*
			* 根据MSDN的说明GetQueuedCompletionStatus()返回TRUE[只]表示从IOCP的队列中取得一个成功完成IO操作的包.
			* 这里"成功"的语义只是指操作这个动作本身成功完成,至于完成的结果是不是程序认为的"成功",不一定.
			* 
			* 1. AcceptEx 和 ConnectEx 成功的话,如果不要求一起发送/接收数据(地址的内容除外),那么 transfered == 0成立.
			* 2. Send, Recv 请求是如果传入的缓冲区长度大于0,而transfered == 0应该判断为失败.
			*
			* 实际测试发现接受客户端连接,执行一个Recv操作,然后客户端马上断开,就能运行到这里,并且 Recv transfered == 0成立.
			* 总而言之,上层应该判断如果传入的数据(不包括AcceptEx和ConnectEx接收的远程地址,而专门指数据部分)缓冲区长度大于0,
			* 而返回的结果表示 transfered = 0 说明操作失败.
			*
			* 网络模块本身无法根据 transfered是否等于0来判断操作是否成功,因为上层完全可能投递一个缓冲区长度为0的Recv请求.
			* 这在服务器开发中是常用的技巧,用来节约内存.
			*
			*/
			instPtr->onIoFinished(context, true, iocpOlpPtr, transfered);
		}
	}
	return 0;
}

void IOCPNetwork::onIoFinished(iocp_context_t* context, bool result, iocp_overlapped_t* iocpOlpPtr, int transfered)
{
	//TRACE("IOCPNetwork::onIoFinished() iocp key 0x%x, socket %d called.\r\n", context, context->s);
	/*
	* 保存状态,关闭定时器,更新统计信息
	*/
	iocp_proc_t func = NULL;
	void* callbackParam = NULL;
	int flags = IOCP_NONE;
	byte* buf = NULL;
	size_t len = 0;

	/*
	* 删除定时器
	*/
	if(iocpOlpPtr->timer != NULL)
	{
		_timerQueue.deleteTimer(iocpOlpPtr->timer, true);
		iocpOlpPtr->timer = NULL;
		/* 定时器删除后,在回调函数执行前,不会再有操作使用 iocpOlpPtr */
	}

	/*
	* 一个异步操作已经返回,保存状态后清除之.
	* 读取套接字状态值,以获得操作失败的原因.
	*/
	if(context->lockPtr) context->lockPtr->rlock();

	if(result) iocpOlpPtr->transfered += transfered;
	iocpOlpPtr->lastCompleteCounter = _hrt.now();

	func = iocpOlpPtr->iocpProc;
	callbackParam = iocpOlpPtr->param;
	flags = iocpOlpPtr->oppType;
	buf = iocpOlpPtr->buf;
	len = iocpOlpPtr->len;

	cleanOlp(iocpOlpPtr);

	if(context->status != IOCP_NORMAL)
	{
		flags |= context->status;
	}
	if(context->lockPtr) context->lockPtr->unlock();
	
	/*
	* 回调结果
	*/
	if(func)
	{
		func(context, flags, result, transfered, buf, len, callbackParam);
	}
}

int IOCPNetwork::accept(iocp_key_t key, SOCKET sockNew, byte* buf, size_t len, size_t timeout, iocp_proc_t func, void* param)
{
	if( !_inited ) return IOCP_UNINITIALIZED;
	if(len < (sizeof(sockaddr_in) + 16) * 2) return IOCP_BUFFERROR; /* 缓冲区长度不够 */
	iocp_context_t* context = reinterpret_cast<iocp_context_t*>(key);
	assert(IOCP_HANDLE_SOCKET == context->type);

	/*
	* 获得AcceptEx()函数指针并调用之,参考MSDN关于AcceptEx的示例代码.
	*/
	SOCKET sockListen = reinterpret_cast<SOCKET>(context->h);
	DWORD dwBytesReceived = 0;
	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	LPFN_ACCEPTEX lpfnAcceptEx = NULL;
	DWORD dwBytes = 0;
	if( 0 != WSAIoctl(sockListen, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidAcceptEx, sizeof(GuidAcceptEx), &lpfnAcceptEx, sizeof(lpfnAcceptEx), &dwBytes, NULL, NULL) )
	{
		assert(0);
		_lastErrorCode = WSAGetLastError();
		return IOCP_UNDEFINED;
	}

	/*
	* 设置标记,然后调用AcceptEx,加锁以确保同步正确
	* 由于发送和接收分别对应不同的重叠结构,所以可以用读写锁同时进行,只要套接字状态正常.
	*/
	int ret = IOCP_PENDING;
	if(context->lockPtr) context->lockPtr->rlock();
	if(context->status != IOCP_NORMAL)
	{
		ret = context->status; 
	}
	else if(sessionTimeout(context))
	{
		ret = IOCP_SESSIONTIMEO;
	}
	else
	{
		/* assert检查:不要同时投递多个同类型的操作,由程序逻辑确保这一点,而不用同步量以提高并发量,否则只能用互斥锁了. */
		assert(context->readOlp.oppType == IOCP_NONE);
		context->readOlp.oppType = IOCP_ACCEPT;
		context->readOlp.buf = buf;
		context->readOlp.len = len;
		context->readOlp.realLen = len;
		context->readOlp.iocpProc = func;
		context->readOlp.param = param;
		context->readOlp.timeout = timeout;
		if(timeout > 0)
		{
			/* 先创建定时器,acceptEx失败再删除是有必要的,尤其是在没有同步的情况下 */
			context->readOlp.timer = _timerQueue.createTimer(timeout, readTimeoutProc, context);
		}

		if( !lpfnAcceptEx(sockListen, sockNew, buf, 
			len - (sizeof(sockaddr_in) + 16) * 2 , sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, 
			&dwBytesReceived, reinterpret_cast<LPOVERLAPPED>(&context->readOlp)) )
		{
			if( WSA_IO_PENDING != WSAGetLastError())
			{
				assert(0);
				_lastErrorCode = WSAGetLastError();
				ret = IOCP_UNDEFINED;
			}
		}
		else
		{
			assert(0);
			ret = IOCP_UNDEFINED;
		}

		/*
		* 调用失败,清除标志回收资源
		*/
		if(ret != IOCP_PENDING)
		{
			cleanOlp(&context->readOlp);
		}
	}

	if(context->lockPtr) context->lockPtr->unlock();

	return ret;
}

int IOCPNetwork::realRecv(iocp_context_t* context)
{
	DWORD dwTransfered = 0;
	DWORD dwLastError = 0;
	if(context->type == IOCP_HANDLE_SOCKET)
	{
		DWORD dwFlags = 0;
		WSABUF wsaBuf = { context->readOlp.len, reinterpret_cast<char*>(context->readOlp.buf) };

		SOCKET s = reinterpret_cast<SOCKET>(context->h);
		if(SOCKET_ERROR == WSARecv(s, &wsaBuf, 1, &dwTransfered, &dwFlags, reinterpret_cast<LPWSAOVERLAPPED>(&context->readOlp), NULL))
		{
			dwLastError = WSAGetLastError();
			if(dwLastError != WSA_IO_PENDING)
			{
				_lastErrorCode = dwLastError;
				return IOCP_UNDEFINED;
			}
		}
		return IOCP_PENDING;
	}
	else
	{
		if(!ReadFile(context->h, context->readOlp.buf, context->readOlp.len, &dwTransfered, reinterpret_cast<LPOVERLAPPED>(&context->readOlp)))
		{
			dwLastError = GetLastError();
			if(dwLastError != ERROR_IO_PENDING)
			{
				_lastErrorCode = dwLastError;
				return IOCP_UNDEFINED;
			}
		}
		return IOCP_PENDING;
	}
}

int IOCPNetwork::read(iocp_key_t key, byte* buf, size_t len, size_t timeout, iocp_proc_t func, void* param)
{
	return recv(key, buf, len, timeout, func, param);
}

int IOCPNetwork::recv(iocp_key_t key, byte* buf, size_t len, size_t timeout, iocp_proc_t func, void* param)
{
	if( !_inited ) return IOCP_UNINITIALIZED;
	iocp_context_t* context = reinterpret_cast<iocp_context_t*>(key);

	/*
	* 执行WSARecv
	*/
	int ret = IOCP_PENDING;

	if(context->lockPtr) context->lockPtr->rlock();
	if(context->status != IOCP_NORMAL)
	{
		/* 允许一个接收操作和一个发送操作同时进行 */
		ret = context->status; 
	}
	else if(sessionTimeout(context))
	{
		ret = IOCP_SESSIONTIMEO;
	}
	else
	{
		assert(context->readOlp.oppType == IOCP_NONE);
		context->readOlp.oppType = IOCP_RECV;
		context->readOlp.buf = buf;
		context->readOlp.len = len;
		context->readOlp.timeout = timeout;
		assert(context->readOlp.timer == NULL);
		context->readOlp.iocpProc = func;
		context->readOlp.param = param;
		context->readOlp.realLen = len;

		/*
		* 检查是否超出速度限制,如果是,则延时发送/接收
		*/
		size_t delay = 0;
		if(context->readOlp.speedLmt > 0 && context->readOlp.lastCompleteCounter != 0)
		{
			/* 
			* 按照最大速度限制,发送 nSended数据完成应该在 nExpectTime 这个时间点完成.
			*/
			__int64 expectTime = _hrt.getCounters(static_cast<__int64>(context->readOlp.transfered * 1.0 / context->readOlp.speedLmt * 1000));
			expectTime += context->startCounter;

			if(expectTime > context->readOlp.lastCompleteCounter) /* 完成的时间点提前,说明速度超标 */
			{
				/* 计算出定时器触发的时间. */
				delay = static_cast<size_t>(_hrt.getMs(expectTime - context->readOlp.lastCompleteCounter));
				if(delay > IOCP_MAXWAITTIME_ONSPEEDLMT)
				{
					/* 超过最长等待时间,下一次发送一个最小包. */
					delay = IOCP_MAXWAITTIME_ONSPEEDLMT;
					if(context->readOlp.len > IOCP_MINBUFLEN_ONSPEEDLMT)
					{
						context->readOlp.realLen = IOCP_MINBUFLEN_ONSPEEDLMT;
					}
				}
				else
				{
					/* 下一次可以接收一个最大包. */
					context->readOlp.realLen = context->readOlp.len;
				}
			}
		}


		/*
		* 直接发送或者设置一个定时器延时发送
		*/
		if(delay > 0)
		{
			/*
			*  设置一个延时操作的定时器,并返回成功. 真正操作的结果等定时器到期后再通过回调函数通知.
			*/
			context->readOlp.oppType = IOCP_DELAY_READ;
			context->readOlp.timer = _timerQueue.createTimer(delay, delayRecvProc, context);
		}
		else
		{
			/*
			* 设置超时定时器,然后接收
			*/
			if(context->readOlp.timeout > 0)
			{
				context->readOlp.timer = _timerQueue.createTimer(context->readOlp.timeout, readTimeoutProc, context);
			}

			ret = realRecv(context);

			/*
			* 如果接收操作失败,应该恢复现场,以便下一次调用可以成功.
			*/
			if(IOCP_PENDING != ret)
			{
				cleanOlp(&context->readOlp);
			}
		}
	}
	if(context->lockPtr) context->lockPtr->unlock();
	
	return ret;
}

int IOCPNetwork::realSend(iocp_context_t* context)
{
	DWORD dwTransfered = 0;
	DWORD dwLastError = 0;

	if(context->type == IOCP_HANDLE_SOCKET)
	{
		WSABUF wsaBuf = { context->writeOlp.realLen, reinterpret_cast<char*>(context->writeOlp.buf) };
		SOCKET s = reinterpret_cast<SOCKET>(context->h);
		if(SOCKET_ERROR == WSASend(s, &wsaBuf, 1, &dwTransfered, 0, reinterpret_cast<LPWSAOVERLAPPED>(&context->writeOlp), NULL))
		{
			dwLastError = WSAGetLastError();
			if(dwLastError != WSA_IO_PENDING)
			{
				_lastErrorCode = dwLastError;
				return IOCP_UNDEFINED;
			}
		}
		return IOCP_PENDING;
	}
	else
	{
		if(!WriteFile(context->h, context->writeOlp.buf, context->writeOlp.len, &dwTransfered, reinterpret_cast<LPOVERLAPPED>(&context->writeOlp)))
		{
			dwLastError = GetLastError();
			if(dwLastError != ERROR_IO_PENDING)
			{
				_lastErrorCode = dwLastError;
				return IOCP_UNDEFINED;
			}
		}
		return IOCP_PENDING;
	}
}

int IOCPNetwork::write(iocp_key_t key, const byte* buf, size_t len, size_t timeout, iocp_proc_t func, void* param)
{
	return send(key, buf, len, timeout, func, param);
}

int IOCPNetwork::send(iocp_key_t key, const byte* buf, size_t len, size_t timeout, iocp_proc_t func, void* param)
{
	if( !_inited ) return IOCP_UNINITIALIZED;
	iocp_context_t* context = reinterpret_cast<iocp_context_t*>(key);

	/*
	* 执行WSARecv
	*/
	int ret = IOCP_PENDING;

	if(context->lockPtr) context->lockPtr->rlock();
	if(context->status != IOCP_NORMAL)
	{
		/* 允许一个接收操作和一个发送操作同时进行 */
		ret = context->status; 
	}
	else if(sessionTimeout(context))
	{
		ret = IOCP_SESSIONTIMEO;
	}
	else
	{
		assert(context->writeOlp.oppType == IOCP_NONE);
		context->writeOlp.oppType = IOCP_SEND;
		context->writeOlp.buf = const_cast<byte*>(buf);
		context->writeOlp.len = len;
		context->writeOlp.timeout = timeout;
		assert(context->writeOlp.timer == NULL);
		context->writeOlp.iocpProc = func;
		context->writeOlp.param = param;
		context->writeOlp.realLen = len;

		/*
		* 检查是否超出速度限制,如果是,则延时发送/接收
		* 第一次操作不检查
		*/
		size_t delay = 0;
		if(context->writeOlp.speedLmt > 0 && context->writeOlp.lastCompleteCounter != 0)
		{
			/* 
			* 按照最大速度限制,发送 nSended数据完成应该在 nExpectTime 这个时间点完成.
			*/
			__int64 expectTime = _hrt.getCounters(static_cast<__int64>(context->writeOlp.transfered * 1.0 / context->writeOlp.speedLmt * 1000));
			expectTime += context->startCounter;

			if(expectTime > context->writeOlp.lastCompleteCounter) /* 完成的时间点提前,说明速度超标 */
			{
				/* 计算出定时器触发的时间. */
				delay = static_cast<size_t>(_hrt.getMs(expectTime - context->writeOlp.lastCompleteCounter));
				if(delay > IOCP_MAXWAITTIME_ONSPEEDLMT)
				{
					/* 超过最长等待时间,下一次发送一个最小包. */
					delay = IOCP_MAXWAITTIME_ONSPEEDLMT;
					if(context->writeOlp.len > IOCP_MINBUFLEN_ONSPEEDLMT)
					{
						context->writeOlp.realLen = IOCP_MINBUFLEN_ONSPEEDLMT;
					}
				}
				else
				{
					/* 下一次可以发送一个最大包. */
					context->writeOlp.realLen = context->writeOlp.len;
				}

				//TRACE(_T("delay send:%dms.\r\n"), delay);
			}
		}

		/*
		* 直接发送或者设置一个定时器延时发送
		*/
		if(delay > 0)
		{
			context->writeOlp.oppType = IOCP_DELAY_WRITE;
			context->writeOlp.timer = _timerQueue.createTimer(delay, delaySendProc, context);
		}
		else
		{
			/*
			* 设置超时定时器,然后发送
			*/
			if(context->writeOlp.timeout > 0)
			{
				context->writeOlp.timer = _timerQueue.createTimer(context->writeOlp.timeout, readTimeoutProc, context);
			}

			ret = realSend(context);

			if( ret != IOCP_PENDING)
			{
				cleanOlp(&context->writeOlp);
			}
		}
	}
	if(context->lockPtr) context->lockPtr->unlock();
	
	return ret;
}

int IOCPNetwork::connect(iocp_key_t key, sockaddr* addr, byte* buf, size_t len, size_t timeout, iocp_proc_t func, void* param)
{
	if( !_inited ) return IOCP_UNINITIALIZED;
	iocp_context_t* context = reinterpret_cast<iocp_context_t*>(key);
	assert(IOCP_HANDLE_SOCKET == context->type);

	/*
	* 取得ConnectEx函数指针
	*/
	SOCKET s = reinterpret_cast<SOCKET>(context->h);
	DWORD dwBytesReceived = 0;
	GUID GuidConnectEx = WSAID_CONNECTEX;
	LPFN_CONNECTEX lpfnConnectEx = NULL;
	DWORD dwBytes = 0;
	if( 0 != WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidConnectEx, sizeof(GuidConnectEx), &lpfnConnectEx, sizeof(lpfnConnectEx), &dwBytes, NULL, NULL) )
	{
		assert(0);
		_lastErrorCode = WSAGetLastError();
		return IOCP_UNDEFINED;
	}

	/*
	* 执行ConnectEx
	*/

	int ret = IOCP_PENDING;
	DWORD bytesSent = 0;
	if(context->lockPtr) context->lockPtr->rlock();
	if(context->status != IOCP_NORMAL)
	{
		ret = context->status; 
	}
	else if(sessionTimeout(context))
	{
		ret = IOCP_SESSIONTIMEO;
	}
	else
	{
		assert(context->writeOlp.oppType == IOCP_NONE);
		context->writeOlp.oppType = IOCP_CONNECT;
		context->writeOlp.buf = buf;
		context->writeOlp.len = len;
		context->writeOlp.iocpProc = func;
		context->writeOlp.param = param;
		context->writeOlp.realLen = len;
		context->writeOlp.timeout = timeout;

		if(timeout > 0)
		{
			context->writeOlp.timer = _timerQueue.createTimer(timeout, writeTimeoutProc, context);
		}

		if( !lpfnConnectEx(s, addr, sizeof(sockaddr), buf, len, &bytesSent, reinterpret_cast<LPOVERLAPPED>(&context->writeOlp)) )
		{
			int errorCode = WSAGetLastError();
			if( WSA_IO_PENDING != errorCode )
			{
				assert(0);
				_lastErrorCode = errorCode;
				ret = IOCP_UNDEFINED;
			}
		}
		else
		{
			assert(0);
			ret = IOCP_UNDEFINED;
		}

		/*
		* 调用失败,清除标志回收资源
		*/
		if(ret != IOCP_PENDING)
		{
			cleanOlp(&context->writeOlp);
		}
	}
	if(context->lockPtr) context->lockPtr->unlock();

	return ret;
}

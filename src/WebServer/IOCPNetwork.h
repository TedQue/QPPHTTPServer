#pragma once
/* Copyright (C) 2011 阙荣文
 *
 * 这是一个开源免费软件,您可以自由的修改和发布.
 * 禁止用作商业用途.
 *
 * 联系原作者: querw@sina.com 
*/

/**************************
*
* 目的: 
* 实现 Windows平台下支持超时控制的,可复用的IOCP网络模型框架(包括发送超时,接收超时,会话超时).
* 实现速度限制.
* 允许同时有一个读操作(accept和recv)和一个写操作(connect和send).
* 不支持同时有多个异步的读操作或者写操作.
*
* 关于同步
* 由于同一时刻只有一种类型的操作,所以可以用读锁.所有涉及到影响另一种类型操作的函数都要用写锁.
* 如果不需要同时进行读和写操作,即任何时刻内只有一个 IO 操作在进行,那么就可以不用同步(add()函数 sync = false 可以关闭同步)
*
* 对调用者的要求
* 1. 必须等待所有异步操作完成后(成功,失败或取消)才能调用remove删除.
* 2. 对于一个套接同一时刻同一种类型操作只有一个.
*/

#include <Winsock2.h>
#include <map>
#include "Lock.h"
#include "TimerQueue.h"

/* 
* 预定义常数,作为函数返回值或者参数,必要时可以进行 '&', '|' 操作.
*/
const int IOCP_BUFFERROR = 0x0001;
const int IOCP_PENDING = 0x0002; /* 操作正在进行中 */
const int IOCP_UNINITIALIZED = 0x0004;
const int IOCP_DESTROYFAILED = 0x0008;
const int IOCP_READTIMEO = 0x0010;
const int IOCP_SEND = 0x0020;
const int IOCP_RECV = 0x0040;
const int IOCP_CONNECT = 0x0080;
const int IOCP_ACCEPT = 0x0100;
const int IOCP_CANCELED = 0x0200;
const int IOCP_BUSY = 0x0400;
const int IOCP_REMOVE = 0x0800;
const int IOCP_WRITETIMEO = 0x1000;
const int IOCP_HANDLE_CLOSED = 0x2000;
const int IOCP_DELAY_READ = 0x4000;
const int IOCP_DELAY_WRITE = 0x8000;
const int IOCP_SESSIONTIMEO = 0x010000;
const int IOCP_ALL = 0x7FFFFFFF; /* 所有常数的集合 */
const int IOCP_UNDEFINED = 0xFFFFFFFF; /* 未定义结果,通过getLastError可以获得错误码 */

/* 其他的预定义常数 */
void* const IOCP_NULLKEY = (void*)0;
const int IOCP_HANDLE_SOCKET = 1;
const int IOCP_HANDLE_FILE = 2;
const int IOCP_SUCESS = 0; /* 操作成功 */
const int IOCP_NONE = 0; /* 空闲状态 */
const int IOCP_NORMAL = 0; /* 套接字状态正常,可以调用所有sock函数 */
const int IOCP_MAXWAITTIME_ONSPEEDLMT = 2000; /* 速度限制时,最多延时2000毫秒,过长的延时可能导致连接被客户端断开 */
const int IOCP_MINBUFLEN_ONSPEEDLMT = 512; /* 速度限制时,最小包的字节数 */

/*
* 类型定义
*/
typedef void* iocp_key_t;
typedef void (*iocp_proc_t)(iocp_key_t k, int flags, bool result, int transfered, byte* buf, size_t len, void* param); 

/*
* IOCPNetwork 定义
*/
class IOCPNetwork : public INoCopy
{
protected:
	/*
	* 重叠结构定义
	*/
	typedef struct iocp_overlapped_t
	{
		OVERLAPPED olp;
		int oppType;
		byte* buf;
		size_t len;
		size_t realLen; /* 实际传递给recv 函数的长度,由于速度限制的原因,可能小于len */
		iocp_proc_t iocpProc;
		void* param; /* 回调函数的额外参数 */
		TimerQueue::timer_t timer; /* 超时定时器或者延时操作定时器 */
		size_t timeout; /* 超时时间 ms */
		size_t speedLmt; /* 速度限制 B/s */
		__int64 lastCompleteCounter; /* 最近一次的完成时间 */
		__int64 transfered; /* 总计传送的字节数 */
	}IOCPOVERLAPPED;

	/*
	* IOCP套接字context
	*/
	typedef struct iocp_context_t
	{
		HANDLE h; /* 文件句柄,可能是 CreateFile 创建的 HANDLE 也可能是 SOCKET 套接字*/
		iocp_overlapped_t readOlp; /* 读缓冲 accept, recv */
		iocp_overlapped_t writeOlp; /* 写缓冲 connect, send */
		RWLock* lockPtr; /* 同步读写锁 */
		int status; /* IOCP套接字状态,读写锁保护的对象 */
		__int64 startCounter; /* 开始时间 */
		unsigned long sessionTimeo; /* 会话超时,毫秒 */
		IOCPNetwork* instPtr; /* 类实例指针,内部使用 */
		int type; /* 句柄类型:文件或者套接字 */
	};
	typedef std::map<HANDLE, iocp_context_t*> context_map_t;

	/*
	* 内部数据成员
	* _tids: 工作线程ID数组
	* _inited: 是否已经初始化过,如果否,所有key的操作都应该失败.
	*
	* 同步对象池,多个套接字公用一个或几个同步对象,只需和CPU数量相等即可.
	* 如果使用读写锁可以最大限度提高并发执行的效率.
	* 读写锁的原则是: 只涉及到一个方向(读 accept, recv 或者写 connect, send)的操作只要加读锁. 涉及到两个方向的操作要加写锁
	*/
	HANDLE _iocpHandle;
	int _threads;
	uintptr_t* _tids;
	bool _inited; 
	DWORD _lastErrorCode;

	context_map_t _contextMap;
	Lock _lock;
	TimerQueue _timerQueue;
	HighResolutionTimer _hrt;
	LockPool<RWLock> _lockPool;
	
	/*
	* 各种线程函数或者定时器回调函数.
	* 参数都是 iocp_context_t* 类型的值.
	*/
	static unsigned int __stdcall serviceProc(void* lpParam);
	static void __stdcall readTimeoutProc(void* param, unsigned char);
	static void __stdcall writeTimeoutProc(void* param, unsigned char);
	static void __stdcall delaySendProc(void* param, unsigned char);
	static void __stdcall delayRecvProc(void* param, unsigned char);

	/*
	* 异步操作完成时,工作线程调用onIOFinish使得IOCPNetwork有机会在回调给上层时处理一些事务.
	* 比如删除超时定时器等.
	*/
	void onIoFinished(iocp_context_t* context, bool result, iocp_overlapped_t* olp, int transfered);

	/*
	* 内部辅助函数
	*/
	int realSend(iocp_context_t* context);
	int realRecv(iocp_context_t* context);
	iocp_context_t* allocContext();
	void freeContext(iocp_context_t* context);
	void cleanOlp(iocp_overlapped_t* olp);
	bool sessionTimeout(iocp_context_t* context);
	iocp_key_t add(HANDLE h, unsigned long sessionTimeo, size_t readSpeedLmt, size_t writeSpeedLmt, bool isFile, bool sync);
	void closeHandle(iocp_context_t *context);

public:
	IOCPNetwork();
	virtual ~IOCPNetwork();

	/*
	* 启动或者清除 WS2 库.
	*/
	static bool initWinsockLib(WORD nMainVer, WORD nSubVer);
	static bool cleanWinsockLib();

	/*
	* 初始化和销毁
	* threads: 工作线程的个数,如果 threads = 0 则创建当前CPU数等同数量的线程数.
	* 返回值: 预定义常量中的一个.
	*/
	int init(int threads);
	int destroy();
	inline DWORD getLastError() { return _lastErrorCode; }

	/* 
	* 关联套接字句柄到完成端口句柄.
	* 返回值: 预定义常量中的一个.
	*/
	iocp_key_t add(SOCKET s, unsigned long sessionTimeo, size_t readSpeedLmt, size_t writeSpeedLmt, bool sync = false);
	iocp_key_t add(HANDLE f, unsigned long sessionTimeo, size_t readSpeedLmt, size_t writeSpeedLmt, bool sync = false);
	int remove(iocp_key_t key);
	int cancel(iocp_key_t key);
	bool busy(iocp_key_t key);
	SOCKET getSocket(iocp_key_t key);
	bool refresh(iocp_key_t key); /* 重置会话超时,只有在空闲状态下才能刷新成功 */

	/*
	* 异步操作函数
	* 参数s: 套接字句柄.
	* 参数key: 调用回调函数时传递参数.
	* accept: 执行异步accept操作.根据MSDN的说明,必须满足 len >= (sizeof(sockaddr_in) + 16) * 2
	* recv: 接收数据.
	*
	* 返回值: 预定义常量中的一个.
	*/
	int accept(iocp_key_t key, SOCKET sockNew, byte* buf, size_t len, size_t timeout, iocp_proc_t func, void* param);
	int connect(iocp_key_t key, sockaddr* addr, byte* buf, size_t len, size_t timeout, iocp_proc_t func, void* param);
	int recv(iocp_key_t key, byte* buf, size_t len, size_t timeout, iocp_proc_t func, void* param);
	int send(iocp_key_t key, const byte* buf, size_t len, size_t timeout, iocp_proc_t func, void* param);
	int read(iocp_key_t key, byte* buf, size_t len, size_t timeout, iocp_proc_t func, void* param);
	int write(iocp_key_t key, const byte* buf, size_t len, size_t timeout, iocp_proc_t func, void* param);
};


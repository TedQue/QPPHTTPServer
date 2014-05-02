/* Copyright (C) 2011 阙荣文
 *
 * 这是一个开源免费软件,您可以自由的修改和发布.
 * 禁止用作商业用途.
 *
 * 联系原作者: querw@sina.com 
*/

#pragma once
#include "IOCPNetwork.h"
#include "FCGIRecord.h"
#include "HTTPDef.h"

/*
* 关于Fast CGI 的运行模式
* 1. 本地模式,在Windows下使用NT的 命名管道.
* 2. 远程模式,使用套接字.

* FastCGI on NT will set the listener pipe HANDLE in the stdin of
* the new process.  The fact that there is a stdin and NULL handles
* for stdout and stderr tells the FastCGI process that this is a
* FastCGI process and not a CGI process.
*/

/*
* Fast CGI 连接工厂,用来管理 FCGI 连接,并创建和管理本地模式下的 FCGI 服务器进程.
* 
* 说明: 为什么要使用独立的管道名.
* 使用独立的管道名的好处是每个连接对应一个 FCGI 进程,如此就可以安全的关闭 FCGI 进程而不用担心影响到其他进程.
*/

/*
* FCGI 连接
*/
typedef struct
{
	unsigned short requestId;
	iocp_key_t comm; /* 通讯句柄 */
	bool cacheAll;
	__int64 idleTime; /* 最后活跃时间,内部使用 */
	void *instPtr; /* FCGIFactory实例指针,回调函数时用,内部使用 */
}fcgi_conn_t;

/*
* 当有空闲连接时的回调函数
* conn == NULL 时,表示获取失败.
*/
typedef void (*fcgi_conn_ready_func_t)(fcgi_conn_t *conn, void *param);

/*
* 函数返回值定义
*/
const int FCGIF_SUCESS = 0;
const int FCGIF_ERROR = 1;

class FCGIFactory : public INoCopy
{
private:

	/*
	* 本地FCGI进程定义
	*/
	typedef struct
	{
		PROCESS_INFORMATION *processInfo; /* 进程句柄 */
		char pipeName[MAX_PATH]; /* 该进程对应的管道名 */
		fcgi_conn_t *conn; /* 该进程对应的连接(一个进程只能有一个连接) */
		uintptr_t thread; /* 创建进程并连接管道的线程 */
		FCGIFactory *instPtr;
	}fcgi_process_context_t;
	typedef std::list<fcgi_process_context_t*> fcgi_process_list_t;

	/*
	* FCGI 服务进程定义
	*/
	typedef struct 
	{
		char type;	/* 0: 本地; 1: 远程 */
		char name[MAX_PATH]; /* ip地址/或者exec path */
		u_int port; /* 端口 */
		fcgi_process_list_t *processList; /* 进程列表 */
	}fcgi_server_context_t;

	/*
	* 等待队列
	*/
	typedef std::pair<fcgi_conn_ready_func_t, void *> fcgi_get_conn_context_t;
	typedef std::list<fcgi_get_conn_context_t> fcgi_get_conn_list_t;
	typedef std::list<fcgi_conn_t*> fcgi_conn_list_t;
	

	/*
	* 内部数据成员
	*/
	bool _inited;
	fcgi_server_context_t *_fcgiServer; /* FCGI进程信息 */
	fcgi_conn_list_t _workingFcgiConnList; /* 正在工作的FCGI连接 */
	fcgi_conn_list_t _idleFcgiConnList; /* 空闲的FCGI连接 */
	fcgi_get_conn_list_t _waitingList; /* 等待空闲连接队列 */
	unsigned short _fcgiRequestIdSeed; /* FCGI Request ID */
	size_t _maxConn; /* 最大连接数 */
	size_t _maxWait; /* 等待空闲连队列最大项数 >> _maxConn */
	std::string _fileExts; /* 扩展名 */
	bool _cacheAll;
	IOCPNetwork *_network;
	IHTTPServer *_httpServer;
	Lock _lock;
	HighResolutionTimer _hrt;

	fcgi_process_context_t* allocProcessContext();
	bool freeProcessContext(fcgi_process_context_t *context);
	fcgi_conn_t* allocConnectionContext();
	bool freeConnectionContext(fcgi_conn_t *conn);
	fcgi_process_context_t* getProcessContext(fcgi_conn_t *conn);

	bool initConnection(fcgi_conn_t *conn);
	void maintain();

	/*
	* 连接回调
	*/
	static void IOCPCallback(iocp_key_t s, int flags, bool result, int transfered, byte* buf, size_t len, void* param);
	static unsigned __stdcall spawnChild(void *param);
	void onConnect(fcgi_conn_t *conn, bool sucess);

public:
	FCGIFactory(IHTTPServer *httpServer, IOCPNetwork *network);
	~FCGIFactory();

	int init(const std::string &name, unsigned int port, const std::string &fileExts, size_t maxConn, size_t maxWait, bool cacheAll);
	int destroy();

	/*
	* 是否处理此url对应的请求
	*/
	bool catchRequest(const std::string &fileName);

	/*
	* 获取FCGI连接,如果返回值为NULL,则表示无法立即获得一个空闲连接,已经进入等待队列.
	* 一旦有连接进入空闲状态,将会通过 callbackFunc 函数回调.
	*/
	bool getConnection(fcgi_conn_t *&conn, fcgi_conn_ready_func_t callbackFunc, void *param);

	/*
	* 释放FCGI连接,并指明连接是否依然可用.
	*/
	void releaseConnection(fcgi_conn_t* conn, bool good);
};


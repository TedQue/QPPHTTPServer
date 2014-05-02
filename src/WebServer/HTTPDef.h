/* Copyright (C) 2011 阙荣文
 *
 * 这是一个开源免费软件,您可以自由的修改和发布.
 * 禁止用作商业用途.
 *
 * 联系原作者: querw@sina.com 
*/

#pragma once

#include "fastcgi.h"
#include "ATW.h"
#include "WINFile.h"
#include "IOCPNetwork.h"


#define MAX_WINIO 16384 // 系统允许的最大同时使用 WINFile() 打开的文件数
#define G_BYTES (1024 * 1024 * 1024) // 1GB
#define M_BYTES (1024 * 1024)		 // 1MB
#define K_BYTES 1024				 // 1KB
#define MAX_SOCKBUFF_SIZE 4096 // 接收缓冲区. 
#define FCGI_BUFFER_SIZE 4096 // 不能小于 24, 否则 FCGI 缓冲区不够. 一般应该保证在512以上.
#define MAX_METHODSIZE 100 // 用于保存HTTP方法字符串的长度,不宜超过 200
#define MAX_REQUESTHEADERSIZE (K_BYTES * 10) // 最大请求头长度限制
#define MAX_IP_LENGTH 50 // IP地址的最大长度
#define MIN_SIZE_ONSPEEDLIMITED 512 // 达到速度限制时,发送的最小包字节数.
#define MAX_WAITTIME_ONSPEEDLIMITED 2000 // 达到速度限制时,最多等待多少毫秒发下一个包.这个值如果设置得过长,有可能导致客户端任务服务器没响应
#define ETAG_BYTE_SIZE 5 // 对于内存数据,创建ETag时抽取的字节数.
#define FCGI_CONNECT_TIMEO 5000 // 连接FCGI命名管道的超时时间
#define FCGI_MAX_IDLE_SECONDS 5
#define SERVER_SOFTWARE "Q++ HTTP Server/0.20"
#define MAX_POST_DATA (G_BYTES) // 最多接收1G
#define POST_DATA_CACHE_SIZE (16 * K_BYTES) // 超过32KB的POST DATA将被写入文件
#define FCGI_CACHE_SIZE (16 * K_BYTES)
#define FCGI_PIPE_BASENAME "\\\\.\\pipe\\fast_cgi_ques" // FCGI 命名管道名

// 服务器响应码
#define SC_UNKNOWN 0
#define	SC_OK 200
#define	SC_NOCONTENT 204
#define	SC_PARTIAL 206
#define SC_OBJMOVED 302
#define	SC_BADREQUEST 400
#define	SC_FORBIDDEN 403
#define	SC_NOTFOUND 404
#define	SC_BADMETHOD 405
#define	SC_SERVERERROR 500
#define SC_SERVERBUSY 503

// 返回值定义(错误类型定义)
#define SE_SUCCESS 0
#define SE_RUNING 1 // 正在运行
#define SE_STOPPED 2 // 已经停止
#define SE_NETWORKFAILD 3
#define SE_CREATESOCK_FAILED 100 // 套接字创建失败
#define SE_BIND_FAILED 101 // 绑定端口失败
#define SE_LISTEN_FAILED 102 // listen() 函数调用失败.
#define SE_CREATETIMER_FAILED 103 // 无法创建定时器
#define SE_CREATE_IOCP_FAILED 104
#define SE_INVALID_PARAM 105
#define SE_UNKNOWN 1000


// HTTP 连接对象退出码
typedef enum HTTP_CLOSE_TYPE
{
	CT_SUCESS = 0,

	CT_CLIENTCLOSED = 10, // 客户端关闭了连接
	CT_SENDCOMPLETE, // 发送完成
	CT_SEND_TIMEO,
	CT_RECV_TIMEO,
	CT_SESSION_TIMEO,
	CT_BADREQUEST,
	
	CT_FCGI_SERVERERROR = 20,
	CT_FCGI_CONNECT_FAILED,
	CT_FCGI_SEND_FAILED,
	CT_FCGI_RECV_FAILED,
	CT_FCGI_RECV_TIMEO,
	CT_FCGI_SEND_TIMEO,
	CT_FCGI_ABORT,

	CT_NETWORK_FAILED = 50,
	CT_INTERNAL_ERROR,

	CT_UNKNOWN = 999 // 未知.	
}HTTP_CONNECTION_EXITCODE;

// HTTP 方法
enum HTTP_METHOD
{
	METHOD_UNDEFINE = 0,
	METHOD_GET = 1,
	METHOD_POST,
	METHOD_PUT,
	METHOD_HEAD, // 只返回响应头
	METHOD_DELETE, // 删除
	METHOD_TRACE,
	METHOD_CONNECT,

	METHOD_UNKNOWN = 100
};

// Fast CGI 服务器定义
typedef struct 
{
	char name[MAX_PATH + 1];
	bool status;
	char path[MAX_PATH + 1]; // ip地址(远程模式)或者命令行(本地模式)
	u_short port; // 端口. 0表示是本地模式
	char exts[MAX_PATH + 1]; // 文件扩展名,逗号分隔
	size_t maxConnections; // 最大连接数
	size_t maxWaitListSize; // 等待队列大小
	bool cacheAll;	// 是否缓存数据
}fcgi_server_t;

typedef std::map<std::string, unsigned int> str_int_map_t;
typedef std::vector<std::string> str_vec_t;

// 外部定义的字符串
extern const char* g_HTTP_Content_NotFound;
extern const char* g_HTTP_Bad_Request;
extern const char* g_HTTP_Bad_Method;
extern const char* g_HTTP_Server_Error;
extern const char* g_HTTP_Forbidden;
extern const char* g_HTTP_Server_Busy;

// 把一个时间格式化为 HTTP 要求的时间格式(GMT).
std::string format_http_date(__int64* ltime);
std::string to_hex(const unsigned char* pData, int nSize);
std::string decode_url(const std::string& inputStr);
bool map_method(HTTP_METHOD md, char *str);
bool is_end(const byte *data, size_t len);
std::string get_field(const char* buf, const char* key);
void get_file_ext(const std::string &fileName, std::string &ext);
bool match_file_ext(const std::string &ext, const std::string &extList);
std::string get_last_error(DWORD errCode = 0);
size_t split_strings(const std::string &str, str_vec_t &vec, const std::string &sp);
bool get_ip_address(std::string& str);
std::string format_size(__int64 bytes);
std::string format_speed(__int64 bytes, unsigned int timeUsed);

/*
* HTTP 配置接口
*/
class IHTTPConfig
{
public:
	IHTTPConfig() {};
	virtual ~IHTTPConfig() {};

	virtual std::string docRoot() = 0;
	virtual std::string tmpRoot() = 0;
	virtual std::string defaultFileNames() = 0;
	virtual std::string ip() = 0;
	virtual u_short port() = 0;
	virtual bool dirVisible() = 0;
	virtual size_t maxConnections() = 0;
	virtual size_t maxConnectionsPerIp() = 0;
	virtual size_t maxConnectionSpeed() = 0;
	virtual size_t sessionTimeout() = 0;
	virtual size_t recvTimeout() = 0;
	virtual size_t sendTimeout() = 0;
	virtual size_t keepAliveTimeout() = 0;
	virtual bool getFirstFcgiServer(fcgi_server_t *serverInf) = 0;
	virtual bool getNextFcgiServer(fcgi_server_t *serverInf) = 0;
};

/*
* HTTP Server 的抽象接口
*/
typedef void* conn_id_t;
const conn_id_t INVALID_CONNID = NULL;
class IRequest;
class IResponder;
class IHTTPServer
{
public:
	virtual ~IHTTPServer() {};

	/*
	* 回调函数接口
	*/
	virtual int onRequestDataReceived(IRequest* request, size_t bytesTransfered) = 0;
	virtual int onResponderDataSent(IResponder *responder,size_t bytesTransfered) = 0;
	virtual void onRequest(IRequest* request, int status) = 0;
	virtual void onResponder(IResponder *responder, int status) = 0;

	/*
	* 获取SERVER信息
	*/
	virtual bool mapServerFilePath(const std::string& url, std::string& serverPath) = 0;
	virtual std::string tmpFileName() = 0;
	virtual const std::string& docRoot() = 0;
	virtual bool isDirectoryVisible() = 0;
	virtual const std::string& defaultFileNames() = 0;
	virtual const std::string& ip() = 0;
	virtual u_short port() = 0;
	virtual size_t maxConnectionsPerIp() = 0;
	virtual size_t maxConnections() = 0;
	virtual size_t maxConnectionSpeed() = 0;
	virtual unsigned long sessionTimeout() = 0;
	virtual unsigned long recvTimeout() = 0;
	virtual unsigned long sendTimeout() = 0;
	virtual unsigned long keepAliveTimeout() = 0;
};

class IRequest
{
public:
	virtual ~IRequest(){};

	/* 访问属性 */
	virtual conn_id_t getConnectionId() = 0;
	virtual size_t getTotalRecvBytes() = 0;
	virtual HTTP_METHOD method() = 0;
	virtual std::string uri(bool decode) = 0;
	virtual std::string field(const char* key) = 0;
	virtual bool keepAlive() = 0;
	virtual bool range(__int64 &from, __int64 &to) = 0;
	virtual bool isValid() = 0;
	virtual size_t headerSize() = 0;
	virtual size_t size() = 0;
	virtual size_t contentLength() = 0;
	virtual __int64 startTime() = 0;

	/* 返回请求头 */
	virtual std::string getHeader() = 0;

	/* 读取 POST DATA */
	virtual size_t read(byte* buf, size_t len) = 0;
	virtual bool eof() = 0;

	/* 控制运行 */
	virtual int run(conn_id_t connId, iocp_key_t clientSock, size_t timeout) = 0;
	virtual bool stop(int ec) = 0;
	virtual bool reset() = 0;
};

class IResponder
{
public:
	virtual ~IResponder() {};

	/* 访问属性 */
	virtual conn_id_t getConnectionId() = 0;
	virtual __int64 getTotalSentBytes() = 0;
	virtual __int64 getTotalRecvBytes() = 0;
	virtual int getServerCode() = 0;

	/* 返回响应头 */
	virtual std::string getHeader() = 0;

	/* 控制运行 */
	virtual int run(conn_id_t connId, iocp_key_t clientSock, IRequest *request) = 0;
	virtual bool stop(int ec) = 0;
	virtual bool reset() = 0;
};

// 服务器状态信息接接口
// HTTP服务器在运行期间会调用这个接口的方法以使该接口的实现可以获取时时的HTTP服务器
class IHTTPServerStatusHandler
{
public:
	// 有新连接时调用,由参数 bRefused 标识是否被服务器拒绝.
	virtual void onNewConnection(const char *ip, unsigned int port, bool refused, bool kicked) = 0;
	virtual void onConnectionClosed(const char *ip, unsigned int port, HTTP_CLOSE_TYPE rr) = 0;

	// 数据发送完成时调用
	virtual void onDataSent(const char *ip, unsigned int port, unsigned int bytesSent) = 0;
	virtual void onDataReceived(const char *ip, unsigned int port, unsigned int bytesReceived) = 0;

	// 处理客户端请求,在发送回应到客户端前调用.
	virtual void onRequestBegin(const char *ip, unsigned int port, const char *url, HTTP_METHOD hm) = 0;
	virtual void onRequestEnd(const char *ip, unsigned int port, const char *url, int svrCode, __int64 bytesSent, __int64 bytesRecved, unsigned int timeUsed, bool completed) = 0;
};
/* Copyright (C) 2011 阙荣文
 *
 * 这是一个开源免费软件,您可以自由的修改和发布.
 * 禁止用作商业用途.
 *
 * 联系原作者: querw@sina.com 
*/

#ifndef _H_FILEHEADER_SLOGGER_
#define _H_FILEHEADER_SLOGGER_

/* Simple logger - slogger

简单日志系统

用小规模的代码实现带有以下特质的C++日志系统:
1. 分级的.
2. 可以定位输出到不同的终端/流,如文件,Windows窗口,调试器,控制台等.
3. 线程安全的.
4. 能方便的开启或者关闭某个级别的日志消息(用宏定义实现)

by 阙荣文 - Que's C++ Studio
2010-12-25

*/

/* 

后记

1. 参考了 log4cplus, 有感其庞大的代码规模(对于一个日志系统来说), 于是有写一个 slogger 的想法.
   事实上,有时候(我认为是大多数情况下)要求很简单:给程序加上日志以便调试或者了解程序的运行状况,对诸如格式,套
   接字,LOGGER服务器等并没要求, 如此 log4cplus 显然过于笨重.

2. 曾经有加入"异步日志"的想法,考虑到日志的作用就是要准确,及时的反映程序运行的状态,而"异步"与此背道而驰,遂放弃.

Ted.Que
2010-12-29

*/

/*

版权说明

保留署名, 自由修改,使用.

*/

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include <string>
#include <iostream>
#include <vector>
#include <sstream>
#include <locale>

//
// 宏定义
// 依靠下列预定义的可以很方便的添加或者删除日志输出,提高效率
// 

// log level define
#define LOGGER_LEVEL slogger::LogLevel
#define LL_NONE slogger::ll_none
#define LL_FATAL slogger::ll_fatal
#define LL_ERROR slogger::ll_error
#define LL_WARNING slogger::ll_warning
#define LL_INFO slogger::ll_info
#define LL_DEBUG slogger::ll_debug
#define LL_TRACE slogger::ll_trace
#define LL_ALL slogger::ll_all

// logger disable all
#ifndef LOGGER_DISABLE
#define LOGGER_DECLARE(logger) slogger::Logger logger
#define LOGGER_USING(logger) extern slogger::Logger logger
#define LOGGER_CLOG(logger, ll, fmt, ...) \
	do{if((ll) <= (logger).getLogLevel()) \
	{\
		(logger).log((ll), (fmt), __VA_ARGS__);\
	}}while(0)
#else
#define LOGGER_DEFINE(logger) 
#define LOGGER_USING(logger) 
#define LOGGER_CLOG(logger, ll, fmt, args) 
#endif

// logger disable ll_fatal
#if !defined(LOGGER_DISABLE_FATAL) && !defined(LOGGER_DISABLE)
#define LOGGER_FATAL(logger, msg) (logger)<<slogger::ll_fatal<<##msg##<<(logger)
#define LOGGER_CFATAL(logger, fmt, ...) LOGGER_CLOG((logger), (LL_FATAL), (fmt), __VA_ARGS__)
#else
#define LOGGER_FATAL(logger, msg)
#define LOGGER_CFATAL(logger, fmt, ...)
#endif

// logger disable ll_error
#if !defined(LOGGER_DISABLE_ERROR) && !defined(LOGGER_DISABLE)
#define LOGGER_ERROR(logger, msg) (logger)<<slogger::ll_error<<##msg##<<(logger)
#define LOGGER_CERROR(logger, fmt, ...) LOGGER_CLOG((logger), (LL_ERROR), (fmt), __VA_ARGS__)
#else
#define LOGGER_ERROR(logger, msg)
#define LOGGER_CERROR(logger, fmt, ...)
#endif

// logger disable ll_info
#if !defined(LOGGER_DISABLE_INFO) && !defined(LOGGER_DISABLE)
#define LOGGER_INFO(logger, msg) (logger)<<slogger::ll_info<<##msg##<<(logger)
#define LOGGER_CINFO(logger, fmt, ...) LOGGER_CLOG((logger), (LL_INFO), (fmt), __VA_ARGS__)
#else
#define LOGGER_INFO(logger, msg)
#define LOGGER_CINFO(logger, fmt, ...)
#endif

// logger disable ll_warning
#if !defined(LOGGER_DISABLE_WARNING) && !defined(LOGGER_DISABLE)
#define LOGGER_WARNING(logger, msg) (logger)<<slogger::ll_warning<<##msg##<<(logger)
#define LOGGER_CWARNING(logger, fmt, ...) LOGGER_CLOG((logger), (LL_WARNING), (fmt), __VA_ARGS__)
#else
#define LOGGER_WARNING(logger, msg)
#define LOGGER_CWARNING(logger, fmt, ...)
#endif

// logger disable ll_debug
#if !defined(LOGGER_DISABLE_DEBUG) && !defined(LOGGER_DISABLE)
#define LOGGER_DEBUG(logger, msg) (logger)<<slogger::ll_debug<<##msg##<<(logger)
#define LOGGER_CDEBUG(logger, fmt, ...) LOGGER_CLOG((logger), (LL_DEBUG), (fmt), __VA_ARGS__)
#else
#define LOGGER_DEBUG(logger, msg)
#define LOGGER_CDEBUG(logger, fmt, ...)
#endif

// logger disable ll_trace
#if !defined(LOGGER_DISABLE_TRACE) && !defined(LOGGER_DISABLE)
#define LOGGER_TRACE(logger, msg) (logger)<<slogger::ll_trace<<##msg##<<(logger)
#define LOGGER_CTRACE(logger, fmt, ...) LOGGER_CLOG((logger), (LL_TRACE), (fmt), __VA_ARGS__)
#else
#define LOGGER_TRACE(logger, msg)
#define LOGGER_CTRACE(logger, fmt, ...)
#endif

//
// simple logger with C++
//

namespace slogger /* simple logger*/
{

#if defined(_UNICODE) || defined(UNICODE)
	typedef std::wstring tstring;
	typedef std::wostringstream tstringstream;
	typedef std::wostream tostream;
	typedef wchar_t tchar;
	typedef std::wofstream tofstream;
	#define tcout std::wcout
#else
	typedef std::string tstring;
	typedef std::ostringstream tstringstream;
	typedef std::ostream tostream;
	typedef char tchar;
	typedef std::ofstream tofstream;
	#define tcout std::cout
#endif

	// log level
	enum LogLevel
	{
		ll_none = 0,
		ll_fatal,
		ll_error,
		ll_warning,
		ll_info,
		ll_debug,
		ll_trace,
		ll_all
	};
	const tchar *LOGLEVEL_NAME[];

	// 输出的基类
	class Appender
	{
	public:
		Appender() {};
		virtual ~Appender() {};
		virtual int append(const tstring &logMsg) = 0;
		virtual bool open() = 0;
		virtual void close() = 0;
		virtual bool autoDelete() = 0;
	};
	typedef std::vector<Appender*> appender_list_t;

	// Logger实现
	class Logger
	{
	private:
		tstring _timeFmt; // time format.
		LogLevel _ll; // log level
		LogLevel _llTmp; // for cpp style output
		tstringstream _buf; // for cpp style output
		appender_list_t _appenders; // appender list
		bool _mt; // multiple thread mode
		bool _isOpen; // is open
#ifdef _WIN32
		CRITICAL_SECTION _locker; //multi-thread lock
#else
		pthread_mutex_t _locker;
#endif

	private:
		Logger(const Logger &other); // 含有 appender 指针列表, 禁止拷贝
		Logger& operator = (const Logger &other);

		int _doLog(LogLevel ll, const tchar *msg); // format time string and call appenders to write log.
		int _flush();	// flush() internal buffer to appenders, for cpp style output.

		void _lock(); // multi thread lock
		void _unlock();

	public:
		Logger();
		virtual ~Logger(void);

		// 初始化和设置
		bool open(bool mt = true, LogLevel ll = ll_all, const tstring &timeFmt = _T("%m/%d/%Y-%H:%M:%S"));
		bool close();
		inline bool isOpen() { return _isOpen; }

		// 日志级别
		LogLevel getLogLevel() { return _ll; }
		LogLevel setLogLevel(LogLevel ll);

		// 添加输出器
		int addAppender(Appender *app);

		// 3种预定义输出器
		int addFileAppender(const tstring &fileName, unsigned long maxBytes = 5 * 1024 * 1024, bool multiFile = true); // 预定义的 文件输出器
		int addConsoleAppender(tostream &os = tcout); // 预定义的标准流输出器, 可以是 cout/wcout, cerr/wcerr, clog/wclog
		int addDebugAppender(); // 预定义的调试器输出器.
		bool removeAppender(int appenderHandle);

		// 输出日志
		// cpp style 调用格式: theLogger << ll_info << 1234 << _T("Hello World!) << 0.12 << theLogger;
		// 最后如果忘记 << theLogger 的话,在多线程环境下会引起死锁. 最好可以让编译器提醒.
		// 可以运用宏,避免忘记调用 << theLogger
		tstringstream& operator << (const LogLevel ll); // 开始输入 C++风格的日志. set tmp log level
		friend Logger& operator << (tostream &ss, Logger &logger); // // 结束输入,并清空缓冲中的串输出到日志, 以上两个函数必须成对调用

		// c style 调用格式,和 sprintf 一致 log(ll_info, _T("ip address:%s, port:%d\n"), ipAddress, port);
		int log(LogLevel ll, const tchar *fmt, ...);
	};
}

#endif
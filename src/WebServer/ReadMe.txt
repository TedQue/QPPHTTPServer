V1.5
====

1. 所有需要同步的操作,都调用 HttpFileServer 的成员, 避免死锁. 
不要把 关键段对象 传给线程函数.

2. 需要发送状态信息的, 调用  HttpFileServer 的 OnError() 函数, 传递一个结构体.
包含 N1, N2 和 STR , 只传值, 不传递字符串.
字符串又界面函数处理.
使用 SendMessage() 阻塞处理.


/////////////////
分别定义一个 HTTPRequest 和 HTTPResponse 类, 包装对应的操作.
LinstenProc 和 ServiceProc 只负责套接字操作, 接收发送. 其他不管.

1. HTTPRequest.PushData() 方法接收客户端请求.
2. HTTPResponse.PopData() 弹出响应数据给客户端.
3. HTTPServer中另外写一个函数 GetResponse(),根据HTTPRequest生成一个响应HTTPResponse
即使客户端的参数,格式错误,或者使用了非GET方法,也应该响应,提示客户端.


// 
V1.52Beta Build 152710

1. 在状态栏显示当前使用的带宽.
2. 在状态栏显示当前一共有多少个连接.

3. _wfindfrist 应该改为 _wfindfirst64
4. 超出的日志每次只移除前面100行(原来移除200行)
5. tagClientInf 新增两个字段,分别记录连接开始和连接结束的 GetTickcount 以统计一个连接一共占用了多长时间.
6. HTTPServer 添加 OnRecv() 和 OnSend() 函数,把原来SericeProc中关于接收和发送的代码分别移入这两个函数中,这样逻辑更清晰.
ServiceProc 线程函数只负责驱动 HTTPServer.

7. 允许设置最大连接数

V1.52 Build 152711
OnTimer() 没有正确同步. 修正: 计算带宽时有可能会有微小的误差.

V1.52 Beta 152712
CLIENTINF 关于统计发送字节数应该用 __int64 类型.

V1.52 Beta 152713
下行带宽统计错误,误写了一个变量.

V1.52 Beta 152714
1. 浏览目录时,超过1G的文件显示为 GB 而不是 MB.
2. 设置 WSARecv 和 WSASend 超时,已减少死连接.
3. 统计带宽时,内部使用原子锁同步,以提高效率.(原来使用临界段变量)
4. 连接刚被接受时,只发去一个缓冲区为0的 WSARecv 操作,以节省资源.(在我们这个程序中没有必要.)
5. 第一个IOCP线程兼具AcceptEx()的作用,这样可以节省一个侦听线程,并且可以有效避免死连接.

V1.52 Beta 153715
1. 工具条按钮的颜色.

2. 可以设置每个IP最多多少个连接.
另外做一个map: key = ipaddress string, value = connection count, OnAccept()中检查这个map.

3. 可以设置每个连接的最大带宽.

(1) 在OnSend()中,计算当前连接的带宽,如果超过指定值,那么设置一个定时器,在若干MS后调用 OnSend()发送,而当前执行的OnSend()直接返回.
可能需要另一个TimerQueue
如果等待的时间超过死连接超时,则要修改死连接超时并且增加额外的50MS使下次调用 OnSend()前不会因为速度限制延时发送而被判定为死连接
OnSend()增加一个 bDelay 的参数以标识是否是延时调用.

(2) 另一种方法
在OnSend()中计算当前带宽,如果超过指定值,则计算最小应该发送多少个字节,而不在发送最大缓冲的数据.
使带宽逐渐降低到指定值.

4. 把侦听套接字也用完成端口处理,加大并发处理能力.

5. 连接结束时,计算平均速度.注意除数为0的情况.

6. 修正错误:当一个请求头被分为多次接收时,会导致连接被关闭.

7. 保存是否禁用日志的设置.

8. 允许启用文件日志.

9. 在禁止浏览目录的情况下,允许设置默认文件名

10. 创建3个定时器队列,分别对应3种定时器,以提高性能.

V1.52 Build 152716

V1.52 Build 152718
1. doAccept()中的OnAccept()调用有小问题.
2. 日志信息修改,所有ip地址和端口都放在一行的最前面.

===========
V2.00 Beta

Bug Fix list:
1. 执行网络操作时,如果操作成功返回但是传送的字节数为0判断为是客户端关闭连接.
2. HTTPServer::mapServerFile()中 "strFilePath.end()--" 改为 "--strFilePath.end()" 才能去除最后的"\"以得到正确的路径.
3. 由于限制带宽而延时发送,导致的定时器资源泄漏.并且如果此时客户端关闭了连接将导致死锁.
4. 会话超时时,在不确定另一个线程运行状况的情况下,直接关闭其有可能使用的套接字句柄,将导致一个同步问题. 现在更改会话超时的检测策略.

--------------------
2011-12-04
主要目的: 添加 FastCGI 支持,实现 remote 模式,并考虑 locale 模式扩展.
不管 remote 还是 locale 只是数据通道的区别,上面的核心代码不应该受此影响.

网络驱动模块的原则: 任意时刻,每个套接字只有一个读或者一个写或者一个读一个写操作.

0. 科普 - CGI 和 FastCGI

0.1 什么是CGI? 它是一个标准,详见(http://tools.ietf.org/html/rfc3875),站在Web服务器的角度,如何让我的服务器支持CGI?
0.1.1 在 UNIX 环境下, 通过 pipe(), fork(), execve(), dup(), dup2() 等函数由Web Server进程创建 CGI 子进程,并且把 stdin 和 stdout 重定向到管道,
同时用HTTP参数生成一个 env 表,通过 execve() 传递到 CGI 子进程从而实现 Web Server 和 CGI Process 之间的数据传递.
0.1.2 在Windows 环境下, 使用文件系统: Web Server 把参数写入一个临时文件然后启动 CGI 子进程, CGI 子进程把处理结果输出为指定的另一个文件.

0.2 什么是 FastCGI? 它是一个开放协议,详见(http://www.fastcgi.com/).
0.2.1 FastCGI Application 有本地模式和远程模式两种运行模式. 如果 FastCGI Application 运行在远程,那么FastCGI Application将监听
一个TCP端口, Web Server连接该端口后按照 FastCGI 协议与之交换数据.
0.2.2 如果是 Unix 环境,可以使用 Unix域套接字(类似于管道)交换,如果是Windows环境只能用TCP连接了.
0.2.3 连接建立后,按照 FastCGI 协议交换数据,控制 FastCGI Application 的生存周期,并且连接可以保留,而不是每次都关闭.

以上,可以参考 Lighttpd 的源代码.

1. 要支持POST方法,客户端递交的数据由一个HTTPContent管理, FastCGI 模块从该对象读取数据.

1.1 HTTPContent 负责从客户端读取数据,处理chunked编码? 至少记录统计数据,然后再递交给 FastCGI 连接.
1.2 

2. FastCGI 连接也一起由完成端口线程驱动, 由 CFastCGIConn 封装, 包含在tagClientInf中.

2.1 CFastCGIConn 方法
2.1.1 Connect(),Close()等.
2.1.2 Proc(),作为网络事件完成后的回调函数,在该函数内部完成 FastCGI 协议规定的动作.

3. 是否维持一个 CFastCGIConn 连接池?

4. 是否维持一个 FastCGI Application 的进程池?

2011-12-05

1. 定义 CFastCGIFactory 用来维护本地FastCGI进程池和连接池.
1.1 CFastCGIFactory::GetConnection() 分配一个活跃FCGI连接.
1.2 CFastCGIFactory::Catch() 根据扩展名如PHP判断是否由 FastCGI捕获当前请求,如果不捕获则默认处理.
1.3 CFastCGIFactory捕获请求后,逻辑上所有的后续处理全部由 CFastCGIConn 处理,所以不再生成Response对象,由CFastCGIConn直接操作客户端连接套接字.

2011-12-06

1. 分离网络模块, IOCPNetwork, 负责创建套接字,关闭套接字,管理定时器队列包括死链接超时和会话超时.
上层模块通过 intptr_t (就是SOCKET句柄) 操作,但是不直接操作套接字.
定义一个纯虚接口,用来接受回调事件,如 OnRead() OnRecv() OnClose() OnTimeout() 等.
或者只是一个回调函数resultfunc_t,因为都依赖于GetQueuedCompletionStatus()的返回结果,网络模块并不知道是send,recv还是accept.

或者 IOCPNetwork 不创建套接字,只是通过 registerSocket(bool add_or_remove) 登记/取消 要用IOCP管理的套接字.
这样似乎更合理一些.

2. HTTPServer
2.1 HTTPServer::CatchFastCGIRequest() 捕获FastCGI请求
2.2 HTTPServer::ReleaseFastCGIRequest() FastCGI模块处理完FastCGI请求后,调用该函数以回收资源.

3. 核心代码(除了界面)去除MFC依赖
3.1 用 _beginthreadex 或者 _beginthread 代替 AfxBeginthread
3.2 尽量用运行时库函数(C/C++标准库函数 > Microsoft CRT (MS 对标准库的扩展) > 直接调用Windows API) / 数据结构用STL,不要用MFC的库.
3.3 我现在更倾向于编写ANSI兼容的UTF-8编码的程序而不是UNICODE版,因为函数调用的问题,只有windows平台下有那些 w_**** 函数.
并且我认为 ANSI兼容的程序才是程序的本来面目,对于中文来说内码只是编码方式并不影响程序逻辑,而全部使用UNICODE版本的API不仅小题大作费时费力而且完全没有意义. UNICODE不仅是必要的而且是好的,用UNICODE函数代替ANSI函数重新写代码不仅是没必要的而且是愚蠢的.要解决中文乱码问题
像LINUX一样,编译用UTF-8,函数接口用ANSI,显示环境用UTF-8才是合乎逻辑的,完美的解决方案.

2011-12-07

1. 允许绑定到本机的某个IP地址或者所有,并在日志或者界面的某个位置显示当前绑定的IP地址.

2. 在HTTPServer中包含一个 ServerEnv 作为 asp.net / jsp / php 编程时用到的 "server"对象的实现.
只提供一些静态的,需要对外公布的数据.
2.1 mapPath()
2.2 maxConnections()
2.3 maxSpeed() / maxConnSpeed()
2.4 sessionTimeout()
2.5 deadTimeout()
2.6 rootPath();
2.7 isDirectoryVisible();
2.8 port();
2.9 ipAddress();
2.10 defaultFileNames();
2.11 curConnectionsCount(); // 和 SOCKINFMAP.size() 重复了.

3. 可以把 ServerEnv 传递给普通连接(由 HTTPServer 本身管理) 或者 任何其他支持的连接如FastCGI连接.

4. HTTPApplication 表示这个HTTP应用,而把HTTPServer改名为 HTTPServer 后用来表示HTTP应用中的一个服务,就像在虚拟主机上建多个网站一样.
HTTPServer拥有自己的网络模块,管理自己的所有连接. HTTPApplication 只是维护一个或多个HTTPServer实例.
似乎也没什么必要...HTTPApplication的功能太少了,没有存在的必要,界面代码直接创建多个 HTTPServer实例即可.

2011-12-08

1. HTTPServer 添加一个 ServerEnv 对象,用来维护 "服务器" 相关的静态信息和配置,是 PHP 中 SERVER 的实现,需要把 ServerEnv 对象传递给 
HttpConnection 和 FastCGIConnection.

2. 逻辑上需要 HttpConnection 管理普通的HTTP连接和 FastCGIConnection 管理FastCGI 连接,但是由于 HttpConnection 功能并不多,所以直接把代码
合并到 HTTPServer 中.

3. FastCGIConnection::proc(ServerEnv* svr ...) HTTPServer根据proc()的返回值判断 FastCGI 模块是否已经处理完这个请求以回收资源.

==================
2011-12-21

1. 用优先队列(最小堆)和一个线程实现定时器. 还是不要使用 Timer Queue, Timer Queue使用了太多线程.

创建一个WaitableTimer和一个Event,记录最近的超时时间. 定时器线程使用 Muti-Wait等待着2个对象.
当有新的定时器需要加入时,SetEvent是定时线程醒来,然后比较得到需要等待的时间,再调用 SetWaitbleTimer.


2. 把所有网络相关的功能,发送接收超时,速度限制等都集中的网络模块中.创建一个独立的线程用来执行延时操作,定时器到期后只插入延时队列,然后就返回尽量减少定时器线程的执行时间.

3. 对网络模块的所有操作加锁以实现如下目的
3.1 允许同时有一个接收请求和一个发送请求.
3.2 允许超时控制.
3.3 运行单独控制发送速度限制和接收速度限制.

所有的操作都加锁还是有意义的,因为回调函数并不加锁,虽然网络动作都是顺序执行,但是回调函数可以并行执行.

2011-12-22
1. 网络模块只实现对IOCP多线程模型的最小封装,既不做同步控制也不保存 OVERLAPPED 数据结构.
定义结构 IOCPOVERLAPPED 多一个字段以保存回调函数的地址.
不管怎样设计,要达到并发接收和发送前提下的超时控制,总要同步控制而且上层调用者也需要做同步控制,所以作为网络模块还是做最小设计.

2. 用队列和WaitableTimer实现定时队列. 作为超时,速度限制的定时器.
会话超时还是在 OnSend 和 OnRecv中检测,提高效率.

2012-1-2
1. 用 QueryPerformanceCounter 实现了精度1ms的定时器队列. 虽然对本程序没什么意义.

2012-1-18
1. 又绕回去了... IOCPNetwork 管理重叠结构,提供超时,速度限制功能,否则网络设置如何应用到FCGI模块中?
2. 定时IHTTPServer接口,供其他模块(如FCGI)回调,这样HTTPServer才能获得信息,如已发送的字节数,是否关闭等.


2012-2-5
1. HTTPServer对象只管理侦听套接字,新连接被接受后, HTTPServer对象根据url扩展名的类型创建不同类型的HTTP连接对象.
HTTPConnection 或者 FCGIConnection.
2. HTTPConnection对象管理一个普通的HTTP请求,目录列表或者静态文件.直接操作网络模块,处理回调函数.
3. FCGIConnection对象管理FCGI请求.直接操作网络模块,处理回调函数.
4. HTTPConnection 等连接对象处理完数据之后,回调 HTTPServer 对象的 onClose 接口,用来回收资源.

2012-2-9
1. HTTPContent 包含在 HTTPRequest 和 HTTPResponse 中,对外部不可见.
2. HTTPRequest 和 HTTPResponse 分别提供 push 和 pop 方法,一个只写,一个只读.
3. HTTPResponse 提供 setFile 和 setData 用来设置文件或者数据,隐藏 HTTPContent 的存在.

2012-2-13
1. HTTPRequest 提供 recvFromClient 函数,并且管理socket buffer, HTTPServer对象在接收到新客户连接后,只生成一个 HTTPRequest 对象,
然后调用 recvFromClient(). 网络模块依然由 HTTPServer 管理.
2. HTTPResponse 提供 sendToClient 函数,同理.
3. FCGIResponse 提供 recvFromFCGI, sendToFCGI, sendToClient 函数.
4. 不再有 HTTPConnection 对象的存在.

5. HTTPRequest 和 IResponse 分别提供用于统计的函数,计算接收和发送的数据.


========================================
1. 一个connection分为两个逻辑部分
1.1 HTTPRequest,负责接收和解析HTTP请求,管理对应的网络事件.
1.2 HTTPResponder,负责发送相应,管理对应的网络事件.

2. IHTTPServer 提供 onRequest 和 onResponser 回调.

3. FCGIResponder 和 HTTPResponder 都派生自接口 IResponder

4. HTTPResponseHeader

2012-2-14
1. 支持 keep-alive 选项.现在保留连接用于处理多个请求.

================================

2012-2-17
只剩下FCGIResponder需要处理了.

1. 用一个 memfile 作为FCGI发送缓冲,可以把多个变长的FCGIRecord写入memfile,然后发送整个memfile 之后,再做下一步处理
这样就解决了使用固定缓冲区长度溢出问题.

2. 发送玩 FCGI_BEGIN_REQUEST和 FCGI_PARAMS之后,就进入了最复杂的状态
2.1 从HTTP读取STDIN
2.2 发送STDIN到FCGI
2.3 从FCGI读取STDOUT
2.4 发送STDOUT到HTTP
以上四步可能同时进行,同步控制应该怎么做呢?

2012-2-22
1. 参数发送成功后分为两条线处理 FCGIResponder
1.1 HTTP -> STDIN
1.2 STDOUT -> HTTP
这两条线可以并发进行,只需要两个缓冲区即可.
模仿 FCGIContent , 添加 eof() 只有收到 FCGI_END_REQUEST 之后 eof() 才返回true.

2012-2-23
FCGIWriterPipe - 类似过滤器的方式,把普通数据输入FCGIPipe 得到打包好后的输出数据.
FCGIReaderPipe - 把从FCGIServer收到的数据输入,得到解包后的普通的数据
需要有自己的缓冲区.

2012-2-28
phpinfo()函数已经能成功运行得到结果.
现在需要再考虑程序结构的问题了.
目标:用最少的缓冲区实现.

实现一个类似管道的 socket_buffer_t, 从FCGI server 收到数据后可以写入缓冲,分多次也没问题,大小可以自动增长.

2012-3-2
2.0版本最后要做的事:
1. 去掉 UNICODE, 还是使用 ANSI-UTF8编程比较好. 才是程序的本源. 字符编码仅仅是显示问题.
2. 用 HTTPConf 类保存配置XML格式.


2012-3-6
FCGIResponder 收工. 
现在考虑 FCGIFactory 管理模块的问题
1. 远程FCGI server 连接的统一管理,回收.
2. 本地FCGI 进程的管理,回收.
3. 错误处理和恢复,远程TCP连接端口,本地进程无响应时,应该怎么做.
4. 同一个TCP/PIPE,多个FCGI请求并发时,怎么做.

2012-3-9
由于多个 FCGIResponder 需要共享同一个FCGI连接(套接字或者管道),所以必须由 FCGIFactory 来管理FCGI连接.
对来自不同的 FCGIResponder 的发送请求排队,以确保数据流中 FCGI Record 的完整性.
同时也需要对 接收请求 排队: FCGIFactory 不能无限制的缓存来自 FCGI连接的数据(控制内存数量),
必须在 FCGIResponder 提交接收请求之后才使用 FCGI连接收取数据,并且需要保证 FCGI record 的完整性.

2012-3-11
如果多个 FCGIResponder 复用同一个FCGI连接,那么各个 FCGIResponder 会相互影响,速度快的客户端需要等待速度慢的客户端.
因为不能缓存过多数据.
另一种思路:  FCGIFactory 实现一个连接池,对每个 FCGIResponder 分配一个单独的FCGI连接,用完后回收.这样,虽然看似消耗多一些,但是
结构上比较简单,而且相互之间不受影响.
缺点: 并发连接数受到限制,尤其是本地模式下, 无法启动过多的FCGI server进程,如果某个客户端的响应时间长就会导致该FCGI连接(进程)
一直被占用.
在本地用远程模式运行FCGIserver是一个折中方案,就像 nginx 那样.

2012-3-12
对于使用连接池的方案,可以增加一个等待队列并记录入队时间(调用getConnection() 中提供一个回调函数). 但有连接被释放时(即 releaseConnection
被调用时)查看等待队列,如果有 FCGIResponder 正在等待则调用回调函数,同时要检查等待时间,如果等待时间过长,则发送 HTTP 503.
如果等待队列过长,则移除队头记录并发送 HTTP503.

远程模式下,套接字的 connect 动作由 FCGIFactory 来执行还是 FCGIResponder 来执行呢?
逻辑上考虑应该由 FCGIFactory, 问题是 connect 是一个异步操作,如果由 FCGIFactory 执行,则必须要回调操作.

2012-3-13
connect 动作由 FCGIFactory 执行, 创建套接字后,进入忙碌队列,设置FCGI连接状态为 CONNECTING,同时 FCGIResponder 进入等待队列.
连接完成后,回调.
原则: 凡是 getConnection 得到的FCGI连接都是可直接用的连接.

2012-3-14
对FCGI Windows NT下命名管道的使用理解有误.
看来一个 PHP-CGI 进程同时只能处理一个连接(因为只有一个命名管道实例即STDIN),监听命名管道, HTTP server 不可能知道到底是哪一个命名管道实例(PHP-CGI)在处理当前连接,除非使用不同的管道名称.
必须要实现一个进程池,创建和维护 PHP-CGI 进程. FCGIProcessContainer
有多少个 PHP-CGI 进程就有多少个命名管道实例,就能创建多少个FCGI连接而不阻塞.

2012-3-15
每个PHP-CGI进程对应一个单独名称的命名管道,对应一个FCGI连接.
这样做的好处是HTTP服务器可以通过FCGI连接的状态判断对应的那个PHP-CGI进程是否正在处理请求.
目的是: FCGIFactory 要有弹性的管理若干个(比如25个)PHP-CGI进程,在压力低的时候,启动尽可能少的 PHP-CGI进程,一旦发现 空闲的PHP-CGI进程数(即空闲的FCGI连接数)超过一个特定数值(比如5个),则杀死一定比例(比如3个,只保留2个空闲进程在空闲队列内等待下一次使用)并且空闲时间超过一定数值(比如5秒)的PHP-CGI进程.

2012-3-18
FCGIResponder
如果不是因为FCGI server的原因非正常中断,则应该发送一个 FCGI_ABORT请求中断服务.
这样归还到FCGIFactory的连接就可以正常继续使用.

2012-3-19
大量使用回调函数其实是一种很不好的设计,对函数的调用顺序,路径必须要非常清楚.
尤其是在有同步锁的情况下,容易造成死锁.
问题是对应这种网络事件驱动的程序不用回调函数又有什么其他办法呢? 生产者-消费者模型?
越做越觉得软件架构的重要性,可惜我醒悟得太晚.

2012-3-23
为了尽量降低FCGI脚本的运行时间,需要缓存来自FCGI的响应
1. 用 memfile 在内存缓存.
2. 用 tmpfile 在磁盘中缓存.

同样也需要缓存来自HTTP的输入
1. 用 memfile 缓存.
2. post 的文件需要写入磁盘的临时文件.

HTTP Server 要创建一个文件夹用于存放临时文件.

HTTPRequest 是否在收到所有数据之后才回调 IHTTPserver onrequest?
长度短,则放入 memcontent,长则放入 file_content.

2012-3-24
只是FCGIResponder需要接收 request input, 所以只在 FCGIResponder 中缓冲数据.

2012-3-26
HTTPRequest 在接收完POST DATA之后才回调HTTPserver.

2012-4-1
"退出点"
FCGI->Cache 和 Cache->HTTP 两个数据流启动后就一直运行,直到有错误发生或者数据流执行完毕才结束.
每个数据流在IO操作完成的一刻间检查另一个数据流的状态,如果发现另一个数据流已经发生错误,则退出.这"一刻间"就是退出点.

2012-4-6
1. FCGIResponder 退出点在:
1.1 进行下一个网络操作前
1.2 网络操作失败时

2. 会话超时如何实现
HTTPServer为每个连接分配一个定时器,定时器超时时,调用 IRequest::stop() 和 IResponder::stop(),最大问题依然是同步.

3. IOCPNetwork 还需要在斟酌一下同步控制, onIoFinished 中何时删除定时器比较好?

2012-4-9
应该在 mapServerFile() 中处理默认文件名的问题. 对于 HTTPRequest, HTTPResponder, FCGIResponder 等对象来说默认文件名是透明的.

由于系统对应使用 fopen() 同时打开的文件数有限制(2048),如何支持更多的并发连接数呢? 排队?
看看 lighttpd 如何实现.

http://stackoverflow.com/questions/870173/is-there-a-limit-on-number-of-open-files-in-windows

If you use the standard C/C++ POSIX libraries with Windows, the answer is "yes", there is a limit.

However, interestingly, the limit is imposed by the kind of C/C++ libraries that you are using.

I came across with the following JIRA thread (http://bugs.mysql.com/bug.php?id=24509) from MySQL. They were dealing with the same problem about the number of open files.

However, Paul DuBois explained that the problem could effectively be eliminated in Windows by using ...

    Win32 API calls (CreateFile(), WriteFile(), and so forth) and the default maximum number of open files has been increased to 16384. The maximum can be increased further by using the --max-open-files=N option at server startup.

Naturally, you could have a theoretically large number of open files by using a technique similar to database connections-pooling, but that would have a severe effect on performance.

Indeed, opening a large number of files could be bad design. However, some situations call require it. For example, if you are building a database server that will be used by thousands of users or applications, the server will necessarily have to open a large number of files (or suffer a performance hit by using file-descriptor pooling techniques).

My 2 cents.

Luis

直接使用 Windows API 创建/打开/读写/关闭 文件 class WINFile 而不再使用 C的流文件 fopen 等. 然后再使用等待队列进一步增大并发数.

2012-4-10
FCGIResponder 中2个使用了大局部变量的函数 sendPostData() 直接使用发送缓冲 reserve(), initFCGIEnv() 也如此,避免栈溢出.

2012-4-11
XmlDocument 在两处使用递归函数的地方使用栈和循环代替.

2012-4-15
1. XmlDocument 处理 xml协议节点的方式还要在斟酌一下.
2. XMLNode::GetNode 缓冲区越界的问题.

2012-4-16
如何简单的支持 XPath
1. 对于绝对路径使用 /root/child1/child2
2. 对于相对路径使用 ./child1/child2 或者 nodename/child1/child2

目前就先这样好了.

定义一个 class XPath 
1. 构造函数接受一个字符串.
2. bool GetFirst()
3. bool GetNext()
4. bool IsAbsolutePath()

或者 

XPath(const std::string &path, XmlDocument *doc);
XMLHANDLE XPath::GetNode();

问题: XPath 是直接操作 XmlDocument 好还是只是提供解析 XPath 字符串的功能好?

2012-4-17
XMLNode 内码还是UNICODE,用不用 UTF-8?
OS_Conv() 的接口重新设计
int OS_Conv("utf-8", "gb2312", src, srcLen, dest, destLen);
Windows 平台下的具体实现要囊括所有的中文编码.

====================
改进XML模块,减少内存的使用量
1. 分析函数 LoadNode() 直接接受 char字符串输入.
2. 分析函数 LoadNode() 可分块调用,每个状态都可以恢复.
3. 输出函数 GetNode() 可分块调用.

2012-4-22
v0.2版的功能已经实现.
1. IOCPNetwork 和 XmlDocument 两个类还需要再改进.
2. XPath 要实现,这样 xml 模块才能真正发挥作用. 返回节点集的时候,可以用类似智能指针的技术,返回一个 new 的 list指针, 用一个类包装它,析构是自动删除.

===========================
2012-5-17
1.重新整理 FCGIResponder 的三个数据流的代码,现在的代码写得太恶心了.
2.添加全缓冲模式.

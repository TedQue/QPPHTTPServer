// MainFrm.cpp : CMainFrame 类的实现
//

#include "stdafx.h"

#include "WebServer.h"
#include "HTTPConfig.h"

#include "MainFrm.h"
#include "SettingDlg.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define ID_STATUS_CONNECTIONS 10001
#define ID_STATUS_SPEED_UP 10002
#define ID_STATUS_SPEED_DOWN 10003

#define IDT_UPDATE_SPEED 10005 /*刷新带宽数据*/
#define CHECKTIME_SPEED 2000 /*每个多少毫秒刷新一次带宽*/

HTTPServer theServer;
HTTPConfig theConf;

// CMainFrame

IMPLEMENT_DYNAMIC(CMainFrame, CFrameWnd)

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
	ON_WM_CREATE()
	ON_WM_SETFOCUS()
	ON_COMMAND(ID_START, &CMainFrame::OnStart)
	ON_UPDATE_COMMAND_UI(ID_START, &CMainFrame::OnUpdateStart)
	ON_COMMAND(ID_STOP, &CMainFrame::OnStop)
	ON_UPDATE_COMMAND_UI(ID_STOP, &CMainFrame::OnUpdateStop)
	ON_COMMAND(ID_SETTING, &CMainFrame::OnSetting)
	ON_UPDATE_COMMAND_UI(ID_SETTING, &CMainFrame::OnUpdateSetting)
	ON_COMMAND(ID_LOG, &CMainFrame::OnLog)
	ON_UPDATE_COMMAND_UI(ID_LOG, &CMainFrame::OnUpdateLog)
	ON_COMMAND(ID_PAUSE, &CMainFrame::OnPause)
	ON_UPDATE_COMMAND_UI(ID_PAUSE, &CMainFrame::OnUpdatePause)
	ON_COMMAND(ID_APP_EXIT, &CMainFrame::OnAppExit)
	ON_WM_CLOSE()
	ON_MESSAGE(WM_NOTIFY_ICON, &CMainFrame::OnTrayIcon)
	ON_COMMAND(ID_CLEAR_LOG, &CMainFrame::OnClearLog)
	ON_UPDATE_COMMAND_UI(ID_CLEAR_LOG, &CMainFrame::OnUpdateClearLog)
	ON_COMMAND(ID_ENABLE_LOG, &CMainFrame::OnEnableLog)
	ON_UPDATE_COMMAND_UI(ID_ENABLE_LOG, &CMainFrame::OnUpdateEnableLog)
	ON_WM_TIMER()
	ON_MESSAGE(WM_CONNECTION_NUMBER, &CMainFrame::OnConnectionNumber)
	ON_MESSAGE(WM_SETMESSAGESTRING, &CMainFrame::OnSetMessageString)
	ON_WM_DESTROY()
END_MESSAGE_MAP()

static UINT indicators[] =
{
	ID_SEPARATOR,           // 状态行指示器
	ID_SEPARATOR,
	ID_SEPARATOR,
	ID_SEPARATOR,
	//ID_INDICATOR_CAPS,
	//ID_INDICATOR_NUM,
	//ID_INDICATOR_SCRL,
};


// CMainFrame 构造/析构

CMainFrame::CMainFrame() : m_nWindowLogger(0), m_nFileLogger(0)
{
	// TODO: 在此添加成员初始化代码
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

	m_uTimer = 0;
	InitializeCriticalSection(&m_cs);
}

CMainFrame::~CMainFrame()
{
	DeleteObject(m_hIcon);
	DeleteCriticalSection(&m_cs);
}

void CMainFrame::lock()
{
	EnterCriticalSection(&m_cs);
}

void CMainFrame::unlock()
{
	LeaveCriticalSection(&m_cs);
}
std::locale loc("");

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CFrameWnd::OnCreate(lpCreateStruct) == -1)
		return -1;

	// 创建一个视图以占用框架的工作区
	if (!m_wndView.Create(NULL, NULL, AFX_WS_DEFAULT_VIEW,
		CRect(0, 0, 0, 0), this, AFX_IDW_PANE_FIRST, NULL))
	{
		TRACE0("未能创建视图窗口\n");
		return -1;
	}
	
	if (!m_wndToolBar.CreateEx(this, TBSTYLE_FLAT, WS_CHILD | WS_VISIBLE | CBRS_TOP
		| CBRS_GRIPPER | CBRS_TOOLTIPS | CBRS_FLYBY | CBRS_SIZE_DYNAMIC) ||
		!m_wndToolBar.LoadToolBar(IDR_MAINFRAME))
	{
		TRACE0("未能创建工具栏\n");
		return -1;      // 未能创建
	}

	if (!m_wndStatusBar.Create(this) ||
		!m_wndStatusBar.SetIndicators(indicators,
		  sizeof(indicators)/sizeof(UINT)))
	{
		TRACE0("未能创建状态栏\n");
		return -1;      // 未能创建
	}
	else
	{
		m_wndStatusBar.SetPaneInfo(1, ID_STATUS_CONNECTIONS, SBPS_NORMAL, 110);
		m_wndStatusBar.SetPaneInfo(2, ID_STATUS_SPEED_UP, SBPS_NORMAL, 100);
		m_wndStatusBar.SetPaneInfo(3, ID_STATUS_SPEED_DOWN, SBPS_NORMAL, 100);
	}

	// 设置图标
	SetIcon(m_hIcon, TRUE);

	// TODO: 如果不需要可停靠工具栏，则删除这三行
	m_wndToolBar.EnableDocking(CBRS_ALIGN_ANY);
	EnableDocking(CBRS_ALIGN_ANY);
	DockControlBar(&m_wndToolBar);

		
	// 添加托盘图标.
	memset(&m_IconData, 0, sizeof(m_IconData));
	m_IconData.cbSize = sizeof(m_IconData);
	m_IconData.hWnd = m_hWnd;
	m_IconData.uID = 100;
	m_IconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	m_IconData.uCallbackMessage = WM_NOTIFY_ICON;
	m_IconData.uTimeout = 500;
	_tcscpy(m_IconData.szTip, _T("Q++ HTTP Server - by Que's C++ Studio"));
	m_IconData.hIcon = m_hIcon;
	Shell_NotifyIcon(NIM_ADD, &m_IconData);

	// 初始化状态栏
	ResetStatus();

	/*
	* 初始化
	*/

	// 初始化配置模块,配置文件是 EXE 目录下的 conf.xml
	TCHAR szFilePath[520] = {0};
	if( 0 == GetModuleFileName(NULL, szFilePath, 512))
	{
		assert(0);
	}
	else
	{
		TCHAR* pEnd = _tcsrchr(szFilePath, _T('\\'));
		if(pEnd == NULL)
		{
		}
		else
		{
			_tcscpy(pEnd, _T("\\conf.xml"));
			m_ConfFileName = TtoA(szFilePath);
		}
	}
	if(m_ConfFileName.empty()) m_ConfFileName = "conf.xml";
	if(!theConf.load(m_ConfFileName))
	{
		AfxMessageBox(_T("无法读取/生成配置文件 conf.xml "));
		return -1;
	}
	
	// 根据配置初始化日志模块
	theLogger.open(true, theConf.logLevel());
	std::string logFileName = theConf.logFileName();
	if(!logFileName.empty())
	{
		m_nFileLogger = theLogger.addFileAppender(AtoT(logFileName));
	}
	if(theConf.screenLog())
	{
		m_nWindowLogger = theLogger.addAppender(&m_wndView);
	}

	// 是否自动启动
	if(theConf.autoRun())
	{
		OnStart();
	}
	else
	{
	}

	return 0;
}

BOOL CMainFrame::PreCreateWindow(CREATESTRUCT& cs)
{
	if( !CFrameWnd::PreCreateWindow(cs) )
		return FALSE;

	cs.dwExStyle &= ~WS_EX_CLIENTEDGE;
	cs.lpszClass = AfxRegisterWndClass(0);
	return TRUE;
}


// CMainFrame 诊断

#ifdef _DEBUG
void CMainFrame::AssertValid() const
{
	CFrameWnd::AssertValid();
}

void CMainFrame::Dump(CDumpContext& dc) const
{
	CFrameWnd::Dump(dc);
}

#endif //_DEBUG


// CMainFrame 消息处理程序

void CMainFrame::OnSetFocus(CWnd* /*pOldWnd*/)
{
	// 将焦点前移到视图窗口
	m_wndView.SetFocus();
}

BOOL CMainFrame::OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo)
{
	// 让视图第一次尝试该命令
	if (m_wndView.OnCmdMsg(nID, nCode, pExtra, pHandlerInfo))
		return TRUE;

	// 否则，执行默认处理
	return CFrameWnd::OnCmdMsg(nID, nCode, pExtra, pHandlerInfo);
}


void CMainFrame::OnStart()
{
	if( SE_SUCCESS == theServer.run(&theConf, this))
	{
		ResetStatus();
		m_uTimer = SetTimer(IDT_UPDATE_SPEED, CHECKTIME_SPEED, NULL);

		/*
		* 设置状态栏IP地址和端口
		*/
		std::string str;
		if(!get_ip_address(str))
		{
			str = "未知地址";
		}
		_tstring strIp = AtoT(str);
		CString strIPAddrStatus;
		strIPAddrStatus.Format(_T("正在运行 %s:%d"), strIp.c_str(), theConf.port());
		m_wndStatusBar.SetPaneText(0, strIPAddrStatus, TRUE);
	}
}

void CMainFrame::ResetStatus()
{
	SetConnectionsNumber(0);
	m_lBytesSent = 0;
	m_lBytesRecv = 0;
	m_dwLastTick = 0;

	if(m_uTimer != 0)
	{
		KillTimer(m_uTimer);
		m_uTimer = 0;
	}
	m_wndStatusBar.SetPaneText(0, _T("空闲"), TRUE);
	m_wndStatusBar.SetPaneText(2, _T("上行: 0.00 B/s"), TRUE);
	m_wndStatusBar.SetPaneText(3, _T("下行: 0.00 B/s"), TRUE);
}

void CMainFrame::OnUpdateStart(CCmdUI *pCmdUI)
{
	// TODO: Add your command update UI handler code here
	pCmdUI->Enable(!theServer.runing());
}

void CMainFrame::OnStop()
{
	if(theServer.runing())
	{
		int hm = theServer.stop();
		if(hm == SE_SUCCESS)
		{
			ResetStatus();
		}
	}
}

void CMainFrame::OnUpdateStop(CCmdUI *pCmdUI)
{
	// TODO: Add your command update UI handler code here
	pCmdUI->Enable(theServer.runing());
}

void CMainFrame::OnSetting()
{
	CSettingDlg dlg;
	dlg.m_bAutoLaunch = IsAutoLaunch();
	dlg.m_bAutoRun = theConf.autoRun();

	dlg.m_strRoot = AtoT(theConf.docRoot()).c_str();
	dlg.m_strTmpRoot = AtoT(theConf.tmpRoot()).c_str();
	dlg.m_nPort = theConf.port();
	dlg.m_bListFile = theConf.dirVisible();
	dlg.m_strDefName = AtoT(theConf.defaultFileNames()).c_str();

	dlg.m_nMaxConn = theConf.maxConnections();
	dlg.m_nMaxClientConn = theConf.maxConnectionsPerIp();
	dlg.m_fMaxSpeed = static_cast<float>(theConf.maxConnectionSpeed() * 1.0 / 1024);
	dlg.m_nSessionTimeout = theConf.sessionTimeout() / 1000;
	dlg.m_nDeadConnectionTimeout = theConf.recvTimeout() / 1000;
	dlg.m_nKeepAliveTimeout = theConf.keepAliveTimeout() / 1000;

	dlg.m_nLogLevel = theConf.logLevel();
	dlg.m_strLogFileName = AtoT(theConf.logFileName()).c_str();
	dlg.m_bWindowLog = theConf.screenLog();

	fcgi_server_t phpInf;
	memset(&phpInf, 0, sizeof(fcgi_server_t));
	theConf.getFirstFcgiServer(&phpInf);
	dlg.m_bEnablePHP = phpInf.status ? TRUE : FALSE;
	dlg.m_strPHPExts = AtoT(phpInf.exts).c_str();
	dlg.m_strPHPPath = AtoT(phpInf.path).c_str();
	dlg.m_nPHPPort = phpInf.port;
	dlg.m_nPHPMaxConnections = phpInf.maxConnections;
	dlg.m_nPHPMaxWaitListSize = phpInf.maxWaitListSize;
	dlg.m_bCacheAll = phpInf.cacheAll;

	if( IDOK == dlg.DoModal())
	{
		// 更新设置
		AutoLaunch(dlg.m_bAutoLaunch);

		theConf.setAutoRun(dlg.m_bAutoRun == TRUE);
		theConf.setDocRoot(TtoA(dlg.m_strRoot));
		theConf.setTmpRoot(TtoA(dlg.m_strTmpRoot));
		theConf.setDirVisible(dlg.m_bListFile == TRUE);
		theConf.setDefaultFileNames(TtoA(dlg.m_strDefName));
		theConf.setPort(dlg.m_nPort);
		theConf.setMaxConnections(dlg.m_nMaxConn);
		theConf.setMaxConnectionsPerIp(dlg.m_nMaxClientConn);
		theConf.setMaxConnectionSpeed(static_cast<size_t>(dlg.m_fMaxSpeed * 1024));

		theConf.setSessionTimeout(dlg.m_nSessionTimeout * 1000);
		theConf.setRecvTimeout(dlg.m_nDeadConnectionTimeout * 1000);
		theConf.setSendTimeout(dlg.m_nDeadConnectionTimeout * 1000);
		theConf.setKeepAliveTimeout(dlg.m_nKeepAliveTimeout * 1000);

		theConf.setLogLevel(static_cast<slogger::LogLevel>(dlg.m_nLogLevel));
		theConf.setLogFileName(TtoA(dlg.m_strLogFileName));
		theConf.enableScreenLog(dlg.m_bWindowLog == TRUE);

		fcgi_server_t phpInf;
		memset(&phpInf, 0, sizeof(fcgi_server_t));
		strcpy(phpInf.name, "PHP");
		phpInf.status = dlg.m_bEnablePHP == TRUE;
		strncpy(phpInf.exts, TtoA(dlg.m_strPHPExts).c_str(), MAX_PATH);
		strncpy(phpInf.path, TtoA(dlg.m_strPHPPath).c_str(), MAX_PATH);
		phpInf.port = dlg.m_nPHPPort;
		phpInf.maxConnections = dlg.m_nPHPMaxConnections;
		phpInf.maxWaitListSize = dlg.m_nPHPMaxWaitListSize;
		phpInf.cacheAll = dlg.m_bCacheAll == TRUE;

		theConf.updateFcgiServer("PHP", &phpInf);

		// 保存配置文件
		theConf.save(m_ConfFileName);

		// 应用新的设置(关于日志的),其它设置需要重启服务器
		if(!theConf.screenLog())
		{
			if(m_nWindowLogger != 0) theLogger.removeAppender(m_nWindowLogger);
			m_nWindowLogger = 0;
		}
		else
		{
			if(m_nWindowLogger == 0) m_nWindowLogger = theLogger.addAppender(&m_wndView);
		}

		if(theConf.logFileName().empty())
		{
			if(m_nFileLogger != 0) theLogger.removeAppender(m_nFileLogger);
			m_nFileLogger = 0;
		}
		else
		{
			if(m_nFileLogger == 0) m_nFileLogger = theLogger.addFileAppender(AtoT(theConf.logFileName()));
		}

		if(m_nWindowLogger == 0 && m_nFileLogger == 0)
		{
			theLogger.setLogLevel(LL_NONE);
		}
		else
		{
			theLogger.setLogLevel(theConf.logLevel());
		}
	}
}

void CMainFrame::OnUpdateSetting(CCmdUI *pCmdUI)
{
}

BOOL CMainFrame::PreTranslateMessage(MSG* pMsg)
{
	return CFrameWnd::PreTranslateMessage(pMsg);
}

void CMainFrame::OnLog()
{
}

void CMainFrame::OnUpdateLog(CCmdUI *pCmdUI)
{
	// TODO: Add your command update UI handler code here
}

void CMainFrame::OnPause()
{
	// TODO: Add your command handler code here
}

void CMainFrame::OnUpdatePause(CCmdUI *pCmdUI)
{
	// TODO: Add your command update UI handler code here
}

void CMainFrame::OnAppExit()
{
	// TODO: Add your command handler code here
	OnStop();

	Shell_NotifyIcon(NIM_DELETE, &m_IconData);
	CFrameWnd::OnClose();
}

void CMainFrame::OnClose()
{
	// TODO: Add your message handler code here and/or call default
	ShowWindow(SW_HIDE);
}


LRESULT CMainFrame::OnTrayIcon(WPARAM w, LPARAM l)
{
	if( l == WM_LBUTTONUP || l == WM_RBUTTONUP)
	{
		ShowWindow(SW_SHOW);
	}
	return 0;
}

void CMainFrame::OnClearLog()
{
	m_wndView.m_edt.SetSel(0, -1);
	m_wndView.m_edt.ReplaceSel(_T(""), FALSE);
}


void CMainFrame::OnUpdateClearLog(CCmdUI *pCmdUI)
{
	// TODO: 在此添加命令更新用户界面处理程序代码
}


void CMainFrame::OnEnableLog()
{
	// TODO: 在此添加命令处理程序代码
	if(m_nWindowLogger == 0)
	{
		m_nWindowLogger = theLogger.addAppender(&m_wndView);
	}
	else
	{
		theLogger.removeAppender(m_nWindowLogger);
		m_nWindowLogger = 0;
	}
}


void CMainFrame::OnUpdateEnableLog(CCmdUI *pCmdUI)
{
	// TODO: 在此添加命令更新用户界面处理程序代码
	if(m_nWindowLogger == 0)
	{
		pCmdUI->SetCheck(1);
	}
	else
	{
		pCmdUI->SetCheck(0);
	}
}

void CMainFrame::SetConnectionsNumber(int nTotalConnections)
{
	m_nTotalConnections = nTotalConnections;

	// 只能以消息的方式刷新文本,因为一定是另外一个线程在调用SetConnectionsNumber()函数
	// 如果是另一个线程调用, IsWindow()将失败.
	PostMessage(WM_CONNECTION_NUMBER, m_nTotalConnections, 0);
}

LRESULT CMainFrame::OnConnectionNumber(WPARAM w, LPARAM l)
{
	int nConnectionNumber = (int)w;
	TCHAR szConnectionsText[200] = {_T('\0')};
	_stprintf (szConnectionsText, _T("目前有: %d个连接"), nConnectionNumber);
	m_wndStatusBar.SetPaneText(1, szConnectionsText, TRUE);
	return 0;
}

void CMainFrame::onNewConnection(const char *pszIP, unsigned int nPort, bool bRefused, bool bKicked)
{
	// 需要同步.
	if(bKicked)
	{
	}
	else
	{
		if(bRefused)
		{
		}
		else
		{
			lock();
			SetConnectionsNumber(m_nTotalConnections + 1);
			unlock();
		}
	}
}

void CMainFrame::onConnectionClosed(const char *pszIP, unsigned int nPort, HTTP_CLOSE_TYPE rr)
{
	// 需要同步
	lock();
	SetConnectionsNumber(m_nTotalConnections - 1);
	unlock();
}

void CMainFrame::onDataSent(const char *pszIP, unsigned int nPort, unsigned int nBytesSent)
{
	// 需要做同步 InterLockedExchangeAdd
	// InterlockedExchangeAdd64(&m_lBytesSent, nBytesSent);

	lock(); 
	m_lBytesSent += nBytesSent;
	unlock();
}

void CMainFrame::onDataReceived(const char *pszIP, unsigned int nPort, unsigned int nBytesReceived)
{
	// 需要做同步

	// Windows Vista/7 才有这个函数dll
	// InterlockedExchangeAdd64(&m_lBytesRecv, nBytesReceived); 

	// 其它系统用临界段
	lock();
	m_lBytesRecv += nBytesReceived;
	unlock();
}

void CMainFrame::onRequestBegin(const char *pszIP, unsigned int nPort, const char *pszUrl, HTTP_METHOD hm)
{
}

void CMainFrame::onRequestEnd(const char *pszIP, unsigned int nPort, const char *pszUrl, int svrCode, __int64 bytesSent, __int64 bytesRecved, unsigned int nTimeUsed, bool completed)
{
}

void CMainFrame::OnTimer(UINT_PTR nIDEvent)
{
	// TODO: 在此添加消息处理程序代码和/或调用默认值
	if( nIDEvent == m_uTimer )
	{
		// 记录数据,后面再计算

		lock();
		__int64 lBytesSent = m_lBytesSent;
		__int64 lBytesRecv = m_lBytesRecv;
		DWORD dwLastTickCount = m_dwLastTick;
		DWORD dwTickCount = GetTickCount();
		
		m_lBytesRecv = 0;
		m_lBytesSent = 0;
		m_dwLastTick = dwTickCount;
		unlock();


		/*
		__int64 lBytesSent = m_lBytesSent;
		__int64 lBytesRecv = m_lBytesRecv;
		DWORD dwLastTickCount = m_dwLastTick;
		DWORD dwTickCount = GetTickCount();

		InterlockedExchange64(&m_lBytesRecv, 0);
		InterlockedExchange64(&m_lBytesSent, 0);
		m_dwLastTick = dwTickCount;
		*/

		// 更新文本显示,没有同步要求
		if(dwLastTickCount == 0 || dwTickCount <= dwLastTickCount)
		{
			// 第一次不作计算
		}
		else
		{
			CString strSpeedText(_T(""));

			// 计算上行带宽 Sent
			double llSpeed = lBytesSent * 1.0 / (dwTickCount - dwLastTickCount) * 1000;
			if(llSpeed >= G_BYTES)
			{
				strSpeedText.Format(_T("上行: %.2f GB/s"), llSpeed * 1.0 / G_BYTES);
			}
			else if(llSpeed >= M_BYTES)
			{
				strSpeedText.Format(_T("上行: %.2f MB/s"), llSpeed * 1.0 / M_BYTES);
			}
			else if(llSpeed >= K_BYTES)
			{
				strSpeedText.Format(_T("上行: %.2f KB/s"), llSpeed * 1.0 / K_BYTES);
			}
			else
			{
				strSpeedText.Format(_T("上行: %.2f B/s"), llSpeed);
			}
			m_wndStatusBar.SetPaneText(2, strSpeedText, TRUE);
			

			// 计算下行带宽 Received
			llSpeed = lBytesRecv * 1.0 / (dwTickCount - dwLastTickCount) * 1000;
			if(llSpeed >= G_BYTES)
			{
				strSpeedText.Format(_T("下行: %.2f GB/s"), llSpeed * 1.0 / G_BYTES);
			}
			else if(llSpeed >= M_BYTES)
			{
				strSpeedText.Format(_T("下行: %.2f MB/s"), llSpeed * 1.0 / M_BYTES);
			}
			else if(llSpeed >= K_BYTES)
			{
				strSpeedText.Format(_T("下行: %.2f KB/s"), llSpeed * 1.0 / K_BYTES);
			}
			else
			{
				strSpeedText.Format(_T("下行: %.2f B/s"), llSpeed);
			}
			m_wndStatusBar.SetPaneText(3, strSpeedText, TRUE);
		}
	}
	else
	{
		ASSERT(0);
	}

	__super::OnTimer(nIDEvent);
}

LRESULT CMainFrame::OnSetMessageString(WPARAM wParam, LPARAM lParam)
{
	this->m_nIDTracking = (UINT)wParam;
	this->m_nIDLastMessage = (UINT)wParam;
	return 0;
	//return CFrameWnd::OnSetMessageString(wParam, lParam);
}


void CMainFrame::OnDestroy()
{
	__super::OnDestroy();

	if(m_nWindowLogger != 0)
	{
		theLogger.removeAppender(m_nWindowLogger);
		m_nWindowLogger = 0;
	}
}

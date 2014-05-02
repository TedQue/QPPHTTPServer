// MainFrm.h : CMainFrame 类的接口
//


#pragma once

#include "ChildView.h"
#include "HTTPConfig.h"
#include "HttpServer.h"
#define WM_NOTIFY_ICON (WM_USER + 107)
#define WM_CONNECTION_NUMBER (WM_USER + 109) // 有新的连接数目

class CMainFrame : public CFrameWnd, public IHTTPServerStatusHandler
{
	
public:
	CMainFrame();
protected: 
	DECLARE_DYNAMIC(CMainFrame)

// 属性
public:
	std::string m_ConfFileName;

// 重写
public:
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	virtual BOOL OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo);

	// IHTTPServerStatusHandler 的实现
	virtual void onNewConnection(const char *pszIP, unsigned int nPort, bool bRefused, bool bKicked);
	virtual void onConnectionClosed(const char *pszIP, unsigned int nPort, HTTP_CLOSE_TYPE rr);
	virtual void onDataSent(const char *pszIP, unsigned int nPort, unsigned int nBytesSent);
	virtual void onDataReceived(const char *pszIP, unsigned int nPort, unsigned int nBytesReceived);
	virtual void onRequestBegin(const char *pszIP, unsigned int nPort, const char *pszUrl, HTTP_METHOD hm);
	virtual void onRequestEnd(const char *pszIP, unsigned int nPort, const char *pszUrl, int svrCode, __int64 bytesSent, __int64 bytesRecved, unsigned int nTimeUsed, bool completed);

// 实现
public:
	virtual ~CMainFrame();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:  // 控件条嵌入成员
	CStatusBar  m_wndStatusBar;
	CToolBar    m_wndToolBar;
	CChildView    m_wndView;
	int m_nWindowLogger;
	int m_nFileLogger;

	NOTIFYICONDATA m_IconData;
	HICON m_hIcon;

	// 服务器状态相关的变量
	int m_nTotalConnections;
	__int64 m_lBytesSent;
	__int64 m_lBytesRecv;
	DWORD m_dwLastTick; // 上次统计带宽时的系统时间.(因为TIMER不一定准时)
	UINT m_uTimer; // 刷新带宽数据的定时器,1秒一次.
	CRITICAL_SECTION m_cs;
protected:
	void SetConnectionsNumber(int nTotalConnections);
	void ResetStatus();
	void lock();
	void unlock();

// 生成的消息映射函数
protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSetFocus(CWnd *pOldWnd);
	DECLARE_MESSAGE_MAP()
public:
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnStart();
	afx_msg void OnUpdateStart(CCmdUI *pCmdUI);
	afx_msg void OnStop();
	afx_msg void OnUpdateStop(CCmdUI *pCmdUI);
	afx_msg void OnSetting();
	afx_msg void OnUpdateSetting(CCmdUI *pCmdUI);
	afx_msg void OnLog();
	afx_msg void OnUpdateLog(CCmdUI *pCmdUI);
	afx_msg void OnPause();
	afx_msg void OnUpdatePause(CCmdUI *pCmdUI);
	afx_msg void OnAppExit();
	afx_msg void OnClose();
	afx_msg LRESULT OnTrayIcon(WPARAM w, LPARAM l);
	afx_msg void OnClearLog();
	afx_msg void OnUpdateClearLog(CCmdUI *pCmdUI);
	afx_msg void OnEnableLog();
	afx_msg void OnUpdateEnableLog(CCmdUI *pCmdUI);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg LRESULT OnConnectionNumber(WPARAM w, LPARAM l);
	afx_msg LRESULT OnSetMessageString(WPARAM wParam, LPARAM lParam);
	afx_msg void OnDestroy();
};



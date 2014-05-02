// WebServer.cpp : 定义应用程序的类行为。
//

#include "stdafx.h"

#include "MainFrm.h"
#include "WebServer.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CWebServerApp

BEGIN_MESSAGE_MAP(CWebServerApp, CWinApp)
	ON_COMMAND(ID_APP_ABOUT, &CWebServerApp::OnAppAbout)
END_MESSAGE_MAP()


// CWebServerApp 构造

CWebServerApp::CWebServerApp()
{
	// TODO: 在此处添加构造代码，
	// 将所有重要的初始化放置在 InitInstance 中
}


// 唯一的一个 CWebServerApp 对象

CWebServerApp theApp;

// CWebServerApp 初始化

BOOL CWebServerApp::InitInstance()
{
	// 如果一个运行在 Windows XP 上的应用程序清单指定要
	// 使用 ComCtl32.dll 版本 6 或更高版本来启用可视化方式，
	//则需要 InitCommonControlsEx()。否则，将无法创建窗口。
	INITCOMMONCONTROLSEX InitCtrls;
	InitCtrls.dwSize = sizeof(InitCtrls);
	// 将它设置为包括所有要在应用程序中使用的
	// 公共控件类。
	InitCtrls.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&InitCtrls);

	CWinApp::InitInstance();

	if(!IOCPNetwork::initWinsockLib(2, 2))
	{
		AfxMessageBox(IDP_SOCKETS_INIT_FAILED);
	}

	// 若要创建主窗口，此代码将创建新的框架窗口
	// 对象，然后将其设置为应用程序的主窗口对象
	CMainFrame* pFrame = new CMainFrame;
	if (!pFrame) return FALSE;
	m_pMainWnd = pFrame;

	// 创建并加载框架及其资源
	pFrame->LoadFrame(IDR_MAINFRAME, WS_OVERLAPPEDWINDOW | FWS_ADDTOTITLE, NULL, NULL);

	if ( _tcsicmp(this->m_lpCmdLine, _T("hide")) == 0)
	{
		pFrame->ShowWindow(SW_HIDE);
		
	}
	else
	{
		pFrame->ShowWindow(this->m_nCmdShow);
	}
	
	pFrame->UpdateWindow();

	return TRUE;

}

int CWebServerApp::ExitInstance()
{
	// TODO: 在此添加专用代码和/或调用基类
	IOCPNetwork::cleanWinsockLib();
	return CWinApp::ExitInstance();
}

// CWebServerApp 消息处理程序




// 用于应用程序“关于”菜单项的 CAboutDlg 对话框

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// 对话框数据
	enum { IDD = IDD_ABOUTBOX };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持

// 实现
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
END_MESSAGE_MAP()

// 用于运行对话框的应用程序命令
void CWebServerApp::OnAppAbout()
{
	CAboutDlg aboutDlg;
	aboutDlg.DoModal();
}


// CWebServerApp 消息处理程序


#define MY_REGKEY _T("httpserver")
BOOL AutoLaunch(BOOL bRun /* = TRUE */)
{	
	// 获取路径
	TCHAR szFileName[MAX_PATH + 10] = {0};
	szFileName[0] = _T('\"');
	if (0 == GetModuleFileName(NULL, szFileName + 1, MAX_PATH) ) return FALSE;
	_tcscat(szFileName, _T("\" hide")); // 最小化运行

	BOOL bRet = FALSE;
	HKEY hKey;
	LPCTSTR szKeyPath = _T("Software\\Microsoft\\Windows\\CurrentVersion");		
	long ret = RegOpenKeyEx(HKEY_CURRENT_USER, szKeyPath, 0, KEY_WRITE, &hKey);
	if(ret != ERROR_SUCCESS)
	{
		TRACE("无法读取注册表.");
	}
	else
	{
		HKEY hRunKey;
		ret = RegCreateKeyEx(hKey, _T("Run"), 0, NULL, 0, KEY_WRITE, NULL, &hRunKey, NULL);
		if(ERROR_SUCCESS == ret)
		{
			if(bRun)
			{
				bRet = (ERROR_SUCCESS == ::RegSetValueEx(hRunKey, MY_REGKEY, 0, REG_SZ, (BYTE*)szFileName, (_tcslen(szFileName) + 1) * sizeof(TCHAR)));
			}
			else
			{
				ret = RegDeleteValue(hRunKey, MY_REGKEY);
				bRet = (ret == ERROR_SUCCESS);
			}

			RegCloseKey(hRunKey);
		}
		else
		{
			TRACE("无法写注册表.");
		}
		RegCloseKey(hKey);
	}
	return bRet;
}


BOOL IsAutoLaunch()
{
	BOOL bRet = FALSE;
	HKEY hKey;
	TCHAR szKeyPath[] = _T("Software\\Microsoft\\Windows\\CurrentVersion\\Run");		
	long ret = RegOpenKeyEx(HKEY_CURRENT_USER, szKeyPath, 0, KEY_READ, &hKey);
	if(ret != ERROR_SUCCESS)
	{
		TRACE("无法读取注册表.");
	}
	else
	{
		TCHAR szValue[MAX_PATH + 1] = {0};
		DWORD dwType = REG_SZ;
		DWORD dwLen = MAX_PATH * sizeof(TCHAR);

		LONG lRet = ::RegQueryValueEx(hKey, MY_REGKEY, NULL, &dwType, (LPBYTE)szValue, &dwLen);
		if(ERROR_SUCCESS != lRet)
		{
		}
		else
		{
			TCHAR szFileName[MAX_PATH + 10] = {0};
			if (0 == GetModuleFileName(NULL, szFileName + 1, MAX_PATH) )
			{
				TRACE("无法查询获取当前进程的文件名.");
			}
			else
			{
				szFileName[0] = _T('\"');
				_tcscat(szFileName, _T("\" hide"));
				return _tcsicmp(szFileName, szValue) == 0;
			}

		}
		RegCloseKey(hKey);
	}

	return bRet;
}
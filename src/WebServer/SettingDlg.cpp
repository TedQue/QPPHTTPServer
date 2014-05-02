// SettingDlg.cpp : implementation file
//

#include "stdafx.h"
#include "WebServer.h"
#include "SettingDlg.h"

// CSettingDlg dialog

IMPLEMENT_DYNAMIC(CSettingDlg, CDialog)

CSettingDlg::CSettingDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CSettingDlg::IDD, pParent)
	, m_bAutoLaunch(FALSE)
	, m_bAutoRun(FALSE)
	, m_strRoot(_T(""))
	, m_nPort(80)
	, m_bListFile(TRUE)
	, m_nMaxConn(5000)
	, m_nSessionTimeout(0)
	, m_nDeadConnectionTimeout(0)
	, m_nMaxClientConn(0)
	, m_fMaxSpeed(0)
	, m_strDefName(_T("index.html,index.htm,default.html,default.htm"))
	, m_strTmpRoot(_T(""))
	, m_bEnablePHP(FALSE)
	, m_strPHPExts(_T(""))
	, m_strPHPPath(_T(""))
	, m_nPHPPort(0)
	, m_nPHPMaxConnections(0)
	, m_nPHPMaxWaitListSize(0)
	, m_nLogLevel(0)
	, m_strLogFileName(_T(""))
	, m_bWindowLog(FALSE)
	, m_bCacheAll(FALSE)
	, m_nKeepAliveTimeout(0)
{

}

CSettingDlg::~CSettingDlg()
{
}

void CSettingDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Check(pDX, IDC_CHECK2, m_bAutoLaunch);
	DDX_Check(pDX, IDC_CHECK1, m_bAutoRun);
	DDX_Text(pDX, IDC_EDIT1, m_strRoot);
	DDX_Text(pDX, IDC_EDIT2, m_nPort);
	DDV_MinMaxInt(pDX, m_nPort, 1, 65535);
	DDX_Check(pDX, IDC_CHECK3, m_bListFile);
	DDX_Text(pDX, IDC_EDIT3, m_nMaxConn);
	DDV_MinMaxInt(pDX, m_nMaxConn, 1, 100000);
	DDX_Text(pDX, IDC_EDIT4, m_nSessionTimeout);
	DDV_MinMaxInt(pDX, m_nSessionTimeout, 0, 99999);
	DDX_Text(pDX, IDC_EDIT5, m_nDeadConnectionTimeout);
	DDV_MinMaxInt(pDX, m_nDeadConnectionTimeout, 0, 9999);
	DDX_Text(pDX, IDC_EDIT6, m_nMaxClientConn);
	DDV_MinMaxInt(pDX, m_nMaxClientConn, 0, 99999);
	DDX_Text(pDX, IDC_EDIT7, m_fMaxSpeed);
	DDV_MinMaxFloat(pDX, m_fMaxSpeed, 0, 999999);
	DDX_Text(pDX, IDC_EDIT8, m_strDefName);
	DDX_Control(pDX, IDC_CHECK3, m_chkListDir);
	DDX_Control(pDX, IDC_EDIT8, m_edtDftFileName);
	DDX_Text(pDX, IDC_EDIT14, m_strTmpRoot);
	DDV_MaxChars(pDX, m_strTmpRoot, 260);
	DDX_Check(pDX, IDC_CHECK6, m_bEnablePHP);
	DDX_Text(pDX, IDC_EDIT9, m_strPHPExts);
	DDX_Text(pDX, IDC_EDIT10, m_strPHPPath);
	DDV_MaxChars(pDX, m_strPHPPath, 260);
	DDX_Text(pDX, IDC_EDIT11, m_nPHPPort);
	DDV_MinMaxUInt(pDX, m_nPHPPort, 0, 65535);
	DDX_Text(pDX, IDC_EDIT12, m_nPHPMaxConnections);
	DDV_MinMaxInt(pDX, m_nPHPMaxConnections, 0, 9999);
	DDX_Text(pDX, IDC_EDIT13, m_nPHPMaxWaitListSize);
	DDX_CBIndex(pDX, IDC_COMBO1, m_nLogLevel);
	DDX_Control(pDX, IDC_COMBO1, m_cobLogLevel);
	DDX_Text(pDX, IDC_EDIT15, m_strLogFileName);
	DDX_Check(pDX, IDC_CHECK4, m_bWindowLog);
	DDX_Check(pDX, IDC_CHECK5, m_bCacheAll);
	DDX_Text(pDX, IDC_EDIT16, m_nKeepAliveTimeout);
	DDV_MinMaxInt(pDX, m_nKeepAliveTimeout, 0, 99999999);
}


BEGIN_MESSAGE_MAP(CSettingDlg, CDialog)
	ON_BN_CLICKED(IDOK, &CSettingDlg::OnBnClickedOk)
	ON_BN_CLICKED(IDCANCEL, &CSettingDlg::OnBnClickedCancel)
	ON_BN_CLICKED(IDC_CHECK3, &CSettingDlg::OnBnClickedCheck3)
	ON_BN_CLICKED(IDC_CHECK5, &CSettingDlg::OnBnClickedCheck5)
	ON_BN_CLICKED(IDC_CHECK6, &CSettingDlg::OnBnClickedCheck6)
END_MESSAGE_MAP()


// CSettingDlg message handlers

void CSettingDlg::OnBnClickedOk()
{
	// TODO: Add your control notification handler code here
	UpdateData();

	OnOK();
}

void CSettingDlg::OnBnClickedCancel()
{
	// TODO: Add your control notification handler code here
	OnCancel();
}

BOOL CSettingDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	m_cobLogLevel.AddString(_T("LL_NONE"));
	m_cobLogLevel.AddString(_T("LL_FATAL"));
	m_cobLogLevel.AddString(_T("LL_ERROR"));
	m_cobLogLevel.AddString(_T("LL_WARNING"));
	m_cobLogLevel.AddString(_T("LL_INFO"));
	m_cobLogLevel.AddString(_T("LL_DEBUG"));
	m_cobLogLevel.AddString(_T("LL_TRACE"));
	m_cobLogLevel.AddString(_T("LL_ALL"));

	UpdateData(FALSE);
	return TRUE;  // return TRUE unless you set the focus to a control
	// EXCEPTION: OCX Property Pages should return FALSE
}


void CSettingDlg::OnBnClickedCheck3()
{
	// 点击了"允许浏览目录"复选框
	//m_edtDftFileName.SetReadOnly(BST_CHECKED == m_chkListDir.GetCheck());
}


void CSettingDlg::OnBnClickedCheck5()
{
	// 点击了"禁止输出日志"复选框
	/*
	if( BST_CHECKED == m_chkDisableLog.GetCheck() )
	{
		m_chkDisableScreenLog.SetCheck(BST_CHECKED);
	}
	*/
}


void CSettingDlg::OnBnClickedCheck6()
{
	// TODO: 在此添加控件通知处理程序代码
}

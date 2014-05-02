#pragma once
#include "afxwin.h"


// CSettingDlg dialog

class CSettingDlg : public CDialog
{
	DECLARE_DYNAMIC(CSettingDlg)

public:
	CSettingDlg(CWnd* pParent = NULL);   // standard constructor
	virtual ~CSettingDlg();

// Dialog Data
	enum { IDD = IDD_DIALOG_SETTING };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedCancel();
	virtual BOOL OnInitDialog();
	BOOL m_bAutoLaunch;
	BOOL m_bAutoRun;
	CString m_strRoot;
	int m_nPort;
	BOOL m_bListFile;
	int m_nMaxConn;
	int m_nSessionTimeout;
	int m_nDeadConnectionTimeout;
	int m_nMaxClientConn;
	float m_fMaxSpeed;
	afx_msg void OnBnClickedCheck3();
	CString m_strDefName;
	CButton m_chkListDir;
	CEdit m_edtDftFileName;
	afx_msg void OnBnClickedCheck5();
	afx_msg void OnBnClickedCheck6();
	CString m_strTmpRoot;
	BOOL m_bEnablePHP;
	CString m_strPHPExts;
	CString m_strPHPPath;
	UINT m_nPHPPort;
	int m_nPHPMaxConnections;
	UINT m_nPHPMaxWaitListSize;
	int m_nLogLevel;
	CComboBox m_cobLogLevel;
	CString m_strLogFileName;
	BOOL m_bWindowLog;
	BOOL m_bCacheAll;
	int m_nKeepAliveTimeout;
};

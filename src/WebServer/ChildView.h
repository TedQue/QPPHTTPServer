// ChildView.h : CChildView 类的接口
//


#pragma once

// CChildView 窗口

class CChildView : public CWnd, public slogger::Appender
{
// 构造
public:
	CChildView();

// 属性
public:
	CFont m_font;
	CEdit m_edt;
	HBRUSH m_hBlackBrush;

public:
	void SetConsolePos();
	void RefreshConsole();

	// log appender
	virtual int append(const slogger::tstring &logMsg);
	virtual bool open();
	virtual void close();
	virtual bool autoDelete();

// 操作
public:

// 重写
	protected:
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);

// 实现
public:
	virtual ~CChildView();

	// 生成的消息映射函数
protected:
	afx_msg void OnPaint();
	DECLARE_MESSAGE_MAP()
public:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	virtual BOOL Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID, CCreateContext* pContext = NULL);
//	afx_msg void OnDestroy();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnDestroy();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
};


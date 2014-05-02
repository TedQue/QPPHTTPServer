// ChildView.cpp : CChildView 类的实现
//

#include "stdafx.h"
#include "WebServer.h"
#include "ChildView.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define IDC_EDIT 10001


// CChildView

CChildView::CChildView()
{
}

CChildView::~CChildView()
{
}


BEGIN_MESSAGE_MAP(CChildView, CWnd)
	ON_WM_PAINT()
	ON_WM_CREATE()
	ON_WM_SIZE()
//	ON_WM_DESTROY()
	ON_WM_ERASEBKGND()
	ON_WM_DESTROY()
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()



// CChildView 消息处理程序

BOOL CChildView::PreCreateWindow(CREATESTRUCT& cs) 
{
	if (!CWnd::PreCreateWindow(cs))
		return FALSE;

	cs.dwExStyle |= WS_EX_CLIENTEDGE;
	cs.style &= ~WS_BORDER;
	cs.lpszClass = AfxRegisterWndClass(CS_HREDRAW|CS_VREDRAW|CS_DBLCLKS, 
		::LoadCursor(NULL, IDC_ARROW), reinterpret_cast<HBRUSH>(COLOR_WINDOW+1), NULL);

	return TRUE;
}

void CChildView::OnPaint() 
{
	CPaintDC dc(this); // 用于绘制的设备上下文
	
	// TODO: 在此处添加消息处理程序代码
	
	// 不要为绘制消息而调用 
	//CWnd::OnPaint();
	//RefreshConsole();
}
//
//void CChildView::SetConsolePos()
//{
//	int cx, cy;
//	RECT rcClient;
//	GetClientRect(&rcClient);
//	cx = rcClient.right - rcClient.left;
//	cy = rcClient.bottom - rcClient.top;
//
//	if( cx <= 0 || cy <= 0) return;
//
//	CONSOLE_FONT_INFO cfi;
//	GetCurrentConsoleFont(m_hStdout, FALSE, &cfi);
//
//	CONSOLE_SCREEN_BUFFER_INFO csInf;
//	GetConsoleScreenBufferInfo(m_hStdout, &csInf);
//
//	COORD newSize;
//	newSize.X = static_cast<short>((cx) / (cfi.dwFontSize.X) - 4);
//	newSize.Y = csInf.dwSize.Y;
//
//	// 如果要增大 Console 的窗口,则需要先设置 BufferSize 再设置窗口.
//	// 如果要减小,则应该反过来
//	if(newSize.X > csInf.dwSize.X)
//	{
//		SetConsoleScreenBufferSize(m_hStdout, newSize);
//
//		SMALL_RECT swRc;
//		swRc = csInf.srWindow;
//		swRc.Right = newSize.X - 1;
//		swRc.Bottom = static_cast<short>((cy) / (cfi.dwFontSize.Y)) - 1;
//		SetConsoleWindowInfo(m_hStdout, TRUE, &swRc);
//	}
//	else
//	{
//		SMALL_RECT swRc;
//		swRc = csInf.srWindow;
//		swRc.Right = newSize.X - 1;
//		swRc.Bottom = static_cast<short>((cy) / (cfi.dwFontSize.Y)) - 1;
//		SetConsoleWindowInfo(m_hStdout, TRUE, &swRc);
//
//		SetConsoleScreenBufferSize(m_hStdout, newSize);
//	}
//
//	//::SetWindowPos(m_hConsoleWnd, NULL, 0, 0, cx, cy, SWP_SHOWWINDOW | SWP_NOZORDER | SWP_NOMOVE);
//	::MoveWindow(m_hConsoleWnd, 0, 0, cx, cy, TRUE);
//}
//
//void CChildView::RefreshConsole()
//{
//	RECT rc;
//	::GetWindowRect(m_hConsoleWnd, &rc);
//	rc.right -= rc.left;
//	rc.left = 0;
//	rc.bottom -= rc.top;
//	rc.top = 0;
//
//	::InvalidateRect(m_hConsoleWnd, &rc, TRUE);
//	//::RedrawWindow(m_hConsoleWnd, NULL, NULL, RDW_FRAME);
//}

int CChildView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CWnd::OnCreate(lpCreateStruct) == -1)
		return -1;

	//if(AllocConsole())
	//{
	//	m_hConsoleWnd = GetConsoleWindow();
	//	m_hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

	//	//::SetWindowLong(m_hConsoleWnd, GWL_STYLE, WS_CHILD | WS_VISIBLE | WS_VSCROLL);
	//	ModifyStyle(m_hConsoleWnd, WS_BORDER | WS_CAPTION | WS_HSCROLL | WS_DLGFRAME | WS_OVERLAPPEDWINDOW | WS_THICKFRAME,
	//		WS_CHILD | WS_VISIBLE | WS_VSCROLL, 0);
	//	ModifyStyleEx(m_hConsoleWnd, WS_EX_CLIENTEDGE, 0, 0);
	//	::SetParent(m_hConsoleWnd, m_hWnd);

	//	::SetWindowPos(m_hConsoleWnd, NULL, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOZORDER | SWP_NOSIZE);
	//}
	

	CRect rc(0, 0, lpCreateStruct->cx, lpCreateStruct->cy);
	if ( !m_edt.Create(ES_MULTILINE | WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_VSCROLL , rc, this, IDC_EDIT) ) return -1;

	m_font.CreateStockObject(DEFAULT_GUI_FONT);
	m_edt.SetFont(&m_font, FALSE);
	m_edt.SetReadOnly(TRUE);

	m_hBlackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);

	return 0;
}

void CChildView::OnSize(UINT nType, int cx, int cy)
{
	
	CWnd::OnSize(nType, cx, cy);
	
	//SetConsolePos();

	m_edt.MoveWindow(0, 0, cx, cy);
}


BOOL CChildView::Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID, CCreateContext* pContext)
{
	return CWnd::Create(lpszClassName, lpszWindowName, dwStyle, rect, pParentWnd, nID, pContext);
}


//void CChildView::OnDestroy()
//{
//	//FreeConsole();
//	CWnd::OnDestroy();
//}


BOOL CChildView::OnEraseBkgnd(CDC* pDC)
{
	// TODO: 在此添加消息处理程序代码和/或调用默认值
	//RECT rcClient;
	//GetClientRect(&rcClient);
	//
	//pDC->FillSolidRect(0, 0, rcClient.right, rcClient.bottom, 0);
	return TRUE;
}

// 输出日志
int CChildView::append(const slogger::tstring &logMsg)
{
	if (m_edt.m_hWnd == NULL)
	{
		assert(0);
	}
	else
	{
		int nLmt = m_edt.GetLimitText();
		int nTextLength = m_edt.GetWindowTextLength();
		if( (int)(nTextLength + logMsg.size()) >= nLmt )
		{
			int nPos = m_edt.LineIndex(REMOVE_LINE_COUNT);
			m_edt.SetSel(0, nPos);
			m_edt.ReplaceSel(_T(""), FALSE);  // 不能用 Clear(), 由于 Undo() 的原因, 用 Clear() 并没有正在删除数据.
		}

		m_edt.SetSel(nTextLength, nTextLength);
		m_edt.ReplaceSel(logMsg.c_str());
	}
	return 0;
}

bool CChildView::open()
{
	return true;
}

void CChildView::close()
{
}

bool CChildView::autoDelete()
{
	return false;
}

void CChildView::OnDestroy()
{
	__super::OnDestroy();

	// TODO: 在此处添加消息处理程序代码
}


HBRUSH CChildView::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = __super::OnCtlColor(pDC, pWnd, nCtlColor);

	// TODO:  在此更改 DC 的任何特性
	if(pWnd && (pWnd->m_hWnd == m_edt.GetSafeHwnd()))
	{
		pDC->SetBkColor(0);
		pDC->SetTextColor(RGB(225, 225, 225));
		return m_hBlackBrush;
	}
	// TODO:  如果默认的不是所需画笔，则返回另一个画笔
	return hbr;
}

// XmlProcess.cpp: implementation of the XMLDocument class.
//
//////////////////////////////////////////////////////////////////////
// XML 文本分析器
// 1. 目的:完全不依赖MFC,实现一个XML文本解析器
// 2. 优点
// (1) 完全不依赖MFC,可以在任何C++环境使用
// (2) 不依赖文件系统,可以在内存中构建XML文档结构并使用
// 2. 不足
// (1) 由于需要把整个文件都读入到内存中,所以不适合处理很大的XML文件.一般10M以内比较合适
// (2) 对外的借口全部是非UNICODE的,使用时要注意.
// (3) 由于本人对XML标准并不熟悉,只是根据实际用途添加了使用接口,可能有很多XML特性没有实现

// 版权声明: 可以修改或者用于商业用途,但请保留原作者的相关信息.
// 作者: 阙荣文 (querw) / Que's C++ Studio
// 日期: 2006四月

//
//
//

//////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include <vector>
#include <stack>
#include "XmlDocument.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif
//wchar_t wsz[20] = L"\x4E2D\x6587\x0031\x0032\x0033";

// 把分析结果详细输出到调试窗口
#ifdef _DEBUG
//#define VERBOSE_TRACE
#endif

using namespace std;
typedef pair<wstring, wstring> wstr2wstr_t;
typedef list<wstr2wstr_t> list_wstr2wstr_t;		// 属性列表
typedef list_wstr2wstr_t::iterator iter_wstr2wstr_t;

/*
* ANSI <-> UNICODE 转换函数
*/

/*
* 系统相关的字符串转化函数
* Windows 系统 WideCharToMultiByte / MultiByteToWideChar
* Linux 系统 iconv 库函数
*
* dest == NULL 则计算长度
* 输出都不包含 null
*/
#ifdef _WIN32

unsigned int OS_MapCP(const char *codePage)
{
	if(codePage == NULL) return CP_ACP;
	unsigned int cp = 0;

	if(0 == stricmp(codePage, "iso-8859-1")) cp = 28591;
	if(0 == stricmp(codePage, "gb2312")) cp = 936;
	if(0 == stricmp(codePage, "big5")) cp = 950;
	if(0 == stricmp(codePage, "GB18030")) cp = 54936;
	if(0 == stricmp(codePage, "x-Chinese_CNS")) cp = 20000;
	if(0 == stricmp(codePage, "hz-gb-2312")) cp = 52936;
	if(0 == stricmp(codePage, "utf-8")) cp = CP_UTF8;

	CPINFO cpInf;
	memset(&cpInf, 0, sizeof(CPINFO));
	if(GetCPInfo(cp, &cpInf))
	{
		return cp;
	}
	else
	{
		/* 未安装指定的代码页 */
		return CP_ACP;
	}
}

/*
* 把各种非UNICODE字符串转化宽字符串或者相反
* Windows 系统中所谓"宽字符串" = Little Endian UTF-16
*/
int OS_AToW(const char *cp, const char *src, size_t srcLen, wchar_t *dest, size_t destLen)
{
	return MultiByteToWideChar(OS_MapCP(cp), 0, src, srcLen, dest, destLen);
}

int OS_WtoA(const char *cp, const wchar_t *src, size_t srcLen, char *dest, size_t destLen)
{
	return WideCharToMultiByte(OS_MapCP(cp), 0, src, srcLen, dest, destLen, NULL, NULL);
}

#else

// 未实现 iconv
// ..
// ..

#endif

#define STR_ZERO(str, len) memset((str), 0, sizeof(char) * (len))
#define WSTR_ZERO(str, len) memset((str), 0, sizeof(wchar_t) * (len))

string G_W2A(cwchar_t *pwszText)
{
	if(pwszText == NULL) return "";

	// 计算需要分配的字节数
	int nNeedSize = OS_WtoA(NULL, pwszText, wcslen(pwszText), NULL, 0);

	// 实际转换
	string strRet("");
	if(nNeedSize > 0)
	{
		char *pRet = new char[nNeedSize + 1];
		STR_ZERO(pRet, nNeedSize + 1);
		OS_WtoA(NULL, pwszText, wcslen(pwszText), pRet, nNeedSize);
		strRet = pRet;
		delete []pRet;
	}
	return strRet;
}

wstring G_A2W(const char* pszText)
{
	if(pszText == NULL) return L"";

	int nNeedSize = OS_AToW(NULL, pszText, strlen(pszText), NULL, 0);

	wstring strRet(L"");
	if(nNeedSize > 0)
	{
		wchar_t *pRet = new wchar_t[nNeedSize + 1];
		WSTR_ZERO(pRet, nNeedSize + 1);
		OS_AToW(NULL, pszText, strlen(pszText), pRet, nNeedSize);
		strRet = pRet;
		delete []pRet;
	}
	return strRet;
}

bool G_IsBlankChar(cwchar_t ch)
{
	return ch == L' ' || ch == L'\n' || ch == L'\r' || ch == L'\t';
}

bool G_IsBlankChar(char ch)
{
	return ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t';
}

bool G_IsValidText(cwchar_t* pszText)
{
	// 如果一个文本只包含' ' \n \r \t则认为是无效的内容
	cwchar_t *pTmp = pszText;
	int i = 0;
	while(pTmp[i] != 0)
	{
		if(!G_IsBlankChar(pTmp[i]))
		{
			return true;
		}
		++i;
	}
	return false;
}

bool G_IsValidText(const char *pszText)
{
	// 如果一个文本只包含' ' \n \r \t则认为是无效的内容
	const char *pTmp = pszText;
	int i = 0;
	while(pTmp[i] != 0)
	{
		if(!G_IsBlankChar(pTmp[i]))
		{
			return true;
		}
		++i;
	}
	return false;
}

bool G_GetStr(cwchar_t *pBegin, cwchar_t *pEnd, wstring &str) // 截取字符串
{
	int nLen = pEnd - pBegin + 1;
	if(nLen > 0)
	{
		wchar_t *pName = new wchar_t[nLen + 1];
		wcsncpy(pName, pBegin, nLen);
		pName[nLen] = 0;
		str = pName;
		delete []pName;
	}

	return nLen > 0;
}

bool G_IsMatch(cwchar_t *pStr1, cwchar_t *pStr2, size_t nStr2Len)
{
	for(size_t i = 0; i < nStr2Len; ++i)
	{
		if(pStr1[i] == 0 || pStr1[i] != pStr2[i])
		{
			return false;
		}
	}

	return true;
}
#define NSTR_EQUAL(str1, str2) G_IsMatch((str1), (str2), (sizeof(str2) / sizeof(wchar_t) - 1))

size_t G_OutputStr(wchar_t *pDest, size_t off, size_t len, cwchar_t *pszSrc)
{
	size_t srcLen = wcslen(pszSrc);
	if(pDest)
	{
		// 最多复制 len - off 长度的字符串
		if(srcLen > len - off) srcLen = len - off;
		if(srcLen > 0) wcsncpy(pDest + off, pszSrc, srcLen);
	}
	return srcLen;
}

size_t G_OutputStr(wchar_t *pDest, size_t off, size_t len, cwchar_t ch)
{
	int n = 1;
	if(pDest)
	{
		// 最多复制 len - off 长度的字符串
		if(len > off) 
		{
			pDest[off] = ch;
			n = 1;
		}
		else
		{
			n = 0;
		}
	}
	return n;
}
#define OUTPUT_STR(d, off, len, s) G_OutputStr((d), (off), (len), (s))

size_t G_SplitStrings(const std::string &str, std::vector<std::string> &vec, char sp)
{
	std::string srcString(str);
	srcString.push_back(sp); // 额外一个分隔符,方便循环处理.
	std::string::size_type st = 0;
	std::string::size_type stNext = 0;
	while( (stNext = srcString.find(sp, st)) != std::string::npos )
	{
		if(stNext > st)
		{
			vec.push_back(srcString.substr(st, stNext - st));
		}

		// next
		st = stNext + 1;
	}
	return vec.size();
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
enum xmlnode_state	// 分析状态
{
	st_begin = 0,		// 开始

	st_tagstart,	// /*tag开始 - "<"后的第一个字符*/
	st_tagend,		// tag结束 - />,>,?> 前的第一个字符

	st_attrnamestart,	// 属性名开始 - tag后的第一个非空格字符
	st_attrnameend,		// 属性名结束 - =, ,前的第一个字符

	st_attrseparator,	// 属性名和属性值的分隔符 '='

	st_attrvaluestart,	// 属性值开始 - ',",后的第一个字符
	st_attrvalueend,	// 属性值结束 - ',",前的第一个字符

	st_contentstart,	// 内容开始 - >后的第一个字符
	st_contentend,		// 内容结束 - <前的第一个字符

	st_endtagstart,		// 结束TAG 开始 </,<?后的第一个字符
	st_endtagend,		// 结束TAG 结束 >前的第一个字符

	st_commentstart,	// 注释开始 <!--后的第一个字符
	st_commentend,		// 注释结束	-->前的第一个字符

	st_cdatastart,
	st_cdataend,

	st_end,		// 分析结束
};

enum xmlnode_output_state
{
	ost_begin, // 开始
	ost_child, // 正在输出子节点
	ost_endtag, // 输出结尾标签
	ost_end // 结束
};

class XMLNode  
{
private:
	xmlnode_type m_type;		// 节点类型
	wstring m_strName;			// 名称tag
	wstring m_strText;			// 内容 content / comment
	list_wstr2wstr_t m_AttrList;	// 属性列表
	bool m_bHasEndTag; // 是否含有结束标记 <name></name> 这个值仅供参考,如果节点含有子节点,总是带有结束标记.

	XMLNode *m_pParent;			// 父节点指针
	XMLNode *m_pPrevSibling;		// 上一个兄弟节点指针
	XMLNode *m_pNextSibling;		// 下一个兄弟节点指针
	XMLNode *m_pFirstChild;		// 第一个子节点指针
		
	xmlnode_state m_st;			// 分析状态
	xmlnode_output_state m_ost; // 输出状态
	XMLNode *m_pOutputChild; // 正在输出中的子节点

private:
	XMLNode(xmlnode_type xNodeType);
	~XMLNode();
	XMLNode(const XMLNode&);
	XMLNode& operator = (const XMLNode&);

	bool LoadNode(cwchar_t* pszContent, cwchar_t* &pszEnd, bool bKeepPlaceHolder);// 装载节点, pszContent 必须以 0 结尾.
	int GetNode(wchar_t* pBuffer, int nLen); // 输出为宽字符串/或者计算所需字节数
	void DeleteTree(); // 删除以当前节点为根节点的子树
	bool LinkChild(XMLNode *pNode);		// 连接一个子节点
	bool Unlink();						// 把自己从父节点中断开
#ifdef VERBOSE_TRACE
	static void _trace(const char *psz, const wchar_t *pszText);
	static void _trace(const char *pre, wstring &str);
#else
#define _trace(str1, str2)
#endif
	friend class XMLDocument;
};

XMLNode::XMLNode(const xmlnode_type xNodeType)
	: m_type(xNodeType), m_st(st_begin), m_pOutputChild(NULL), m_ost(ost_begin),
	m_pParent(NULL), m_pPrevSibling(NULL), m_pNextSibling(NULL), 
	m_pFirstChild(NULL), m_bHasEndTag(false)
{
}

XMLNode::~XMLNode()
{
	// 如果还有子节点,很有可能有内存泄漏
	// 删除一个节点之前,应该调用 DeleteTree() 把这个节点的子节点全部删除.
	assert(m_pFirstChild == NULL);
}

#ifdef VERBOSE_TRACE
void XMLNode::_trace(const char *psz, const wchar_t *pszText)
{
	TRACE("%s:%s\n", psz, G_W2A(pszText).c_str());
}

void XMLNode::_trace(const char *pre, wstring &str)
{
	_trace(pre, str.c_str());
}
#endif

/*
* 删除以当前节点为根节点的子树(当前节点并不会被删除,自己不能删除自己)
*/
void XMLNode::DeleteTree()
{
	if(!m_pFirstChild)
	{
	}
	else
	{
		// 把所有子节点压栈,然后全部删除
		// 相当于做一次深度优先的遍历
		std::stack<XMLNode*> NodeStack; // 深度优先搜索用的栈.

		// 先把当前节点的所有子节点入栈,作为启动条件
		XMLNode *pChildNode = m_pFirstChild;
		while(pChildNode)
		{
			NodeStack.push(pChildNode);
			pChildNode = pChildNode->m_pNextSibling;
		}

		// 遍历所有节点直到栈内为空
		while(!NodeStack.empty())
		{
			// 访问一个节点
			XMLNode *pCurNode = NodeStack.top();
			NodeStack.pop();

			// 把该节点的所有子节点入栈.
			pChildNode = pCurNode->m_pFirstChild;
			while(pChildNode)
			{
				NodeStack.push(pChildNode);
				pChildNode = pChildNode->m_pNextSibling;
			}

			// 访问完毕,删除节点
			pCurNode->m_pFirstChild = NULL;
			delete pCurNode;
		}

		// 重置当前节点的状态(无子节点)
		m_pFirstChild = NULL;
	}
}

bool XMLNode::LinkChild(XMLNode *pNode)
{
	pNode->m_pParent = this;
	if(m_pFirstChild)
	{
		XMLNode *pChild = m_pFirstChild;
		while(pChild)
		{
			if(pChild->m_pNextSibling == NULL)
			{
				pChild->m_pNextSibling = pNode;
				pNode->m_pPrevSibling = pChild;
				break;
			}
			else
			{
				pChild = pChild->m_pNextSibling;
			}
		}
	}
	else
	{
		m_pFirstChild = pNode;
	}

	return true;
}


bool XMLNode::Unlink()
{
	if(m_pParent)
	{
		if(m_pParent->m_pFirstChild == this)
		{
			m_pParent->m_pFirstChild = m_pNextSibling;
		}
	}

	if(m_pPrevSibling) m_pPrevSibling->m_pNextSibling = m_pNextSibling;
	if(m_pNextSibling) m_pNextSibling->m_pPrevSibling = m_pPrevSibling;

	return true;
}

// pszContent 必须以 null 结尾
// 如果分析成功, pszEnd 指向下一个待分析的字符或者 null.
// 如果分析失败,则pszEnd指向发生错误的字符.
// 如果 bKeepPlaceHolder == true 则节点间的空白字符(缩进格式,换行等)将被保留为子节点.

bool XMLNode::LoadNode(cwchar_t* pszContent, cwchar_t* &pszEnd, bool bKeepPlaceHolder)
{
	/*
	* 为了避免递归调用,分析到子节点时,把父节点压入堆栈,然后循环处理.
	*/
	std::stack<XMLNode*> NodeStack; /* 待分析节点栈 */
	XMLNode *pCurNode = this; /* 当前正在分析的节点 */

	const wchar_t* pCur = pszContent; /* 总是指向下一个要分析的字符 */
	const wchar_t* pBegin = NULL;
	const wchar_t* pEnd = NULL;

	wstr2wstr_t attrName_attrValue; // 临时记录 "属性名=属性值"
	wchar_t chValueFlag;	// ' 或者 " 应该成对出现
	bool bContinue = true;

	/*
	* pszContent 是以 NULL 结尾,所以循环内访问 pCur[0] 和 pCur[1] 是安全的
	*/
	while(pCur[0] != 0)
	{
		switch(pCurNode->m_st)
		{
		case st_begin:
			{
				/*
				* 跳过 '<' 之前的所有空白字符.
				*/
				if(pCur[0] == L'<')
				{
					_trace("+++++++++++++++++++++", L"开始分析节点");

					// 判断节点类型
					if(NSTR_EQUAL(pCur, L"<?"))
					{
						// (1) "<?" 开头的是XML节点
						++pCur; // 跳过 '?'
						pCurNode->m_type = et_xml;
						pCurNode->m_st = st_tagstart;
					}
					else if(NSTR_EQUAL(pCur, L"<!--"))
					{
						// (2) "<!--" 开头的是注释节点
						pCur += 3;
						pCurNode->m_type = et_comment;
						pCurNode->m_st = st_commentstart;
					}
					else if(NSTR_EQUAL(pCur, L"<![CDATA["))
					{
						// (3) "<![CDATA[" 开头 "]]>"结尾的是CDATA部件
						pCur += 8;
						pCurNode->m_type = et_cdata;
						pCurNode->m_st = st_cdatastart;
					}
					else
					{
						// (4) 其他节点
						pCurNode->m_type = et_normal;
						pCurNode->m_st = st_tagstart;
					}	
				}
				else
				{
					// 忽略所有'<'之前的空白字符
					if(G_IsBlankChar(*pCur))
					{
						// 空白字符可以跳过
					}
					else
					{
						// 非法字符,停止分析
						bContinue = false;
						break;
					}
				}
			}
			break;
		case st_tagstart:
			{
				// 记录标记名的开始位置
				pBegin = pCur;
				pEnd = NULL;
				pCurNode->m_st = st_tagend;
				--pCur; // 回退一个字符
			}
			break;
		case st_tagend:
			{
				// <tag 直到遇到 ' ' 或者 '>' 或者 "/>" 或者 "?>" 表示节点名结束	
				// "/>" 和 "?>" 统一在下一个状态切换
				if(NSTR_EQUAL(pCur, L"/>") && pCurNode->m_type == et_normal || NSTR_EQUAL(pCur, L"?>") && pCurNode->m_type == et_xml
					|| G_IsBlankChar(pCur[0]) || pCur[0] == L'>')
				{
					pEnd = pCur - 1;
					pCurNode->m_st = st_attrnamestart;
					--pCur;
				}
				else
				{
					// 非法tag名字符在此判断
					if(pCur[0] == L'<' || pCur[0] == L'/' || pCur[0] == L'?') 
					{
						bContinue = false;
						break;
					}
				}

				// 得到节点名称 <tag_name>
				if(pEnd != NULL)
				{
					if(G_GetStr(pBegin, pEnd, pCurNode->m_strName))
					{
						_trace("节点名", pCurNode->m_strName);
						pBegin = NULL;
						pEnd = NULL;
					}
					else
					{
						pCur = pBegin;
						bContinue = false;
						break;
					}
				}
			}
			break;
		case st_attrnamestart:
			{
				// 查找并记录下属性名的起始地址
				if(G_IsBlankChar(pCur[0]))
				{
					// 跳过属性名前的空白字符
				}
				else if(L'>' == pCur[0])
				{
					pCurNode->m_st = st_contentstart;
				}
				else if(NSTR_EQUAL(pCur, L"/>") && pCurNode->m_type == et_normal || NSTR_EQUAL(pCur, L"?>") && pCurNode->m_type == et_xml)
				{
					pCurNode->m_st = st_end;
					++pCur;
				}
				else
				{
					// 其他字符标识属性名的起始地址
					pBegin = pCur;
					pEnd = NULL;
					pCurNode->m_st = st_attrnameend;
					--pCur;
				}
			}
			break;
		case st_attrnameend:
			{
				// 查找并标识属性名的结束地址:出现 '=' 或者空白字符.
				if(L'=' == pCur[0] || G_IsBlankChar(pCur[0]))
				{
					pCurNode->m_st = st_attrseparator;
					pEnd = pCur - 1;
					--pCur; // 回退 '=' 留给 st_attrseparator 分析
				}
				else
				{
					// 检查属性名中是否出现了非法的字符
				}

				if(pEnd)
				{
					attrName_attrValue.first = L"";
					attrName_attrValue.second = L"";
					if(G_GetStr(pBegin, pEnd, attrName_attrValue.first))
					{
						_trace("属性名", attrName_attrValue.first);
					}
					else
					{
						// 非法的属性名
						bContinue = false;
						break;
					}
				}
			}
			break;
		case st_attrseparator:
			{
				if(G_IsBlankChar(pCur[0]))
				{
					// 过滤掉 '=' 前的多余的空白字符
				}
				else if(L'=' == pCur[0])
				{
					// 查找到了分隔符,开始查找属性值的起始地址
					pCurNode->m_st = st_attrvaluestart;
				}
				else
				{
					// 属性名和分隔符中间除了空格不能有别的字符
					bContinue = false;
					break;
				}
			}
			break;
		case st_attrvaluestart:
			{
				if(G_IsBlankChar(pCur[0]))
				{
					// 过滤掉 '=' 后, ''' '"' 前的多余的空白字符
				}
				else if(L'\'' == pCur[0] || L'\"' == pCur[0])
				{
					// 记录属性值的起始位置
					chValueFlag = pCur[0];	// 记录'/"要成对出现
					pBegin = pCur + 1; // 跳过 ''' 或者 '"'
					pEnd = NULL;
					pCurNode->m_st = st_attrvalueend;
				}
				else
				{
					// 属性名 '=' 字符后有非法的字符(只允许有空格)
					bContinue = false;
					break;
				}
			}
			break;
		case st_attrvalueend:
			{
				// 定位属性值的结束地址:成对出现的 ''' 或者 '"' 的第二个
				if(pCur[0] == chValueFlag)
				{
					assert(chValueFlag == L'\'' || chValueFlag == L'"');
					pEnd = pCur - 1;
					G_GetStr(pBegin, pEnd, attrName_attrValue.second); // 属性值允许是空值,所以不检测 G_GetStr() 的返回值
					pCurNode->m_AttrList.push_back(attrName_attrValue);
					_trace("属性值", attrName_attrValue.second);

					// 分析下一个属性, 由于有单引号或者双引号,所以pCur要下跳一个字符
					if( L' ' == pCur[1] || L'>' == pCur[1] ||
						NSTR_EQUAL(&pCur[1], L"/>") && pCurNode->m_type == et_normal ||
						NSTR_EQUAL(&pCur[1], L"?>") && pCurNode->m_type == et_xml)
					{
						pCurNode->m_st = st_attrnamestart;
					}
					else
					{
						// 属性值\"/\'之后发现非法字符
						bContinue = false;
						break;
					}
					
				}
				else
				{
					// 非法的属性值字符在此判断
				}
			}
			break;
		case st_contentstart:
			{
				// <name attrname='attrvalue'>content</name>
				// 不过虑空格
				pBegin = pCur;
				pEnd = NULL;
				pCurNode->m_st = st_contentend;
				--pCur;
			}
			break;
		case st_contentend:
			{
				if(L'<' == pCur[0])
				{
					// 普通文本也作为一个子节点
					wstring strText;
					pEnd = pCur - 1;
					if(G_GetStr(pBegin, pEnd, strText))
					{
						if(G_IsValidText(strText.c_str()))
						{
							XMLNode *pNode = new XMLNode(et_text);
							pNode->m_strText = strText;
							pCurNode->LinkChild(pNode);
							_trace("普通文本", strText);
						}
						else
						{
							if(bKeepPlaceHolder)
							{
								XMLNode *pNode = new XMLNode(et_positionholder);
								pNode->m_strText = strText;
								pCurNode->LinkChild(pNode);
								_trace("空白占位符", NULL);
							}
						}
					}

					pBegin = NULL;
					pEnd = NULL;

					// 内容结束了,判断下一步操作
					if(L'/' == pCur[1] && pCurNode->m_type == et_normal || L'?' ==pCur[1] && pCurNode->m_type == et_xml)
					{						
						pCurNode->m_st = st_endtagstart;
						++pCur; // 跳过 / 或者 ?
					}
					else
					{
						// 开始分析子节点前把当前节点压栈.
						XMLNode *pNode = new XMLNode(et_normal);
						pCurNode->LinkChild(pNode);
						NodeStack.push(pCurNode);

						pCurNode = pNode;
						pCurNode->m_st = st_begin;
						--pCur; // 子节点从"<"开始,所以回退1格

						_trace("--------------------", L"开始分析子节点");
					}
				}
				else
				{
					// 非法的内容字符在此判断
				}
			}
			break;
		case st_cdatastart:
			{
				pBegin = pCur;
				pEnd = NULL;
				pCurNode->m_st = st_cdataend;
				--pCur;
			}
			break;
		case st_cdataend:
			{
				if(NSTR_EQUAL(pCur, L"]]>"))
				{
					pEnd = pCur - 1;
					G_GetStr(pBegin, pEnd, pCurNode->m_strText); // CDATA文本也作为一个子节点
					_trace("CDATA内容", pCurNode->m_strText);

					// cdata结束了,判断下一步操作
					pCur += 2;
					pCurNode->m_st = st_end;
				}
				else
				{
					// 非法的CDATA字符在此判断
				}
			}
			break;
		case st_commentstart:
			{
				pBegin = pCur;
				pCurNode->m_st = st_commentend;
				pEnd = NULL;
				--pCur;
			}
			break;
		case st_commentend:
			{
				if(NSTR_EQUAL(pCur, L"-->"))
				{
					pEnd = pCur - 1;
					G_GetStr(pBegin, pEnd, pCurNode->m_strText);
					_trace("注释内容", pCurNode->m_strText);

					// 注释节点结束
					pCur += 2;
					pCurNode->m_st = st_end;
				}
				else
				{
					// 非法的注释字符在此判断
					// 连续出现 "--" 为非法
				}
			}
			break;
		case st_endtagstart:
			{
				// 结束标签 </tagname>
				pBegin = pCur;
				pEnd = NULL;
				pCurNode->m_st = st_endtagend;
				--pCur;
			}
			break;
		case st_endtagend:
			{
				if(L'>' == pCur[0])
				{
					pEnd = pCur - 1;
					wstring strTag;
					G_GetStr(pBegin, pEnd, strTag);
					_trace("结束标签", strTag);
					if(strTag == pCurNode->m_strName) 
					{
						pCurNode->m_bHasEndTag = true;
						pCurNode->m_st = st_end;
					}
					else 
					{
						bContinue = false;
						break;
					}
				}
			}
			break;
		case st_end:
			{
				_trace("************************", L"一个节点分析完毕");
				// 当前节点分析完毕,从栈内取出父节点继续分析
				if(!NodeStack.empty())
				{
					pCurNode = NodeStack.top();
					NodeStack.pop();

					pCurNode->m_st = st_contentstart;

					--pCur;
				}
				else
				{
					bContinue = false;
					break;
				}
			}
			break;
		default:
			{
			}
			break;
		}

		/*
		* 继续分析下一个字符
		*/
		if(bContinue)
		{
			++pCur;
		}
		else
		{
			break;
		}
	}

	/*
	* 返回
	*/
	pszEnd = pCur;
	if(this->m_st == st_end)
	{
		return true;
	}
	else
	{
		// 即使读取失败,也可能已经有一部分子节点已经插入,所以在返回前删除之.
		DeleteTree();
		return false;
	}
}

//把节点输出成宽字符串
// 返回值是实际写入的字符个数(0表示输出失败)(不包括0)
// pBuffer == NULL 用于计算长度不包含 null
int XMLNode::GetNode(wchar_t* pBuffer, int nLen)
{
	XMLNode *pCurNode = this;
	std::stack<XMLNode*> NodeStack; /* 待输出节点栈 */
	int nPos = 0;

	// 初始化输出状态
	pCurNode->m_ost = ost_begin;
	pCurNode->m_pOutputChild = NULL;
	
	while(pCurNode)
	{
		switch(pCurNode->m_ost)
		{
		case ost_begin:
			{
				if(pCurNode->m_type == et_text || pCurNode->m_type == et_positionholder)
				{
					// 文本节点,直接返回
					nPos += OUTPUT_STR(pBuffer, nPos, nLen - nPos, pCurNode->m_strText.c_str());
					pCurNode->m_ost = ost_end;
				}
				else if(pCurNode->m_type == et_comment)
				{	
					// 注释节点输出 <!--注释内容-->
					nPos += OUTPUT_STR(pBuffer, nPos, nLen, L"<!--");
					nPos += OUTPUT_STR(pBuffer, nPos, nLen, pCurNode->m_strText.c_str());
					nPos += OUTPUT_STR(pBuffer, nPos, nLen, L"-->");
					pCurNode->m_ost = ost_end;
				}
				else if(pCurNode->m_type == et_cdata)
				{
					// CDATA 节点输出 <![CDATA[内容]]>
					nPos += OUTPUT_STR(pBuffer, nPos, nLen, L"<![CDATA[");
					nPos += OUTPUT_STR(pBuffer, nPos, nLen, pCurNode->m_strText.c_str());
					nPos += OUTPUT_STR(pBuffer, nPos, nLen, L"]]>");
					pCurNode->m_ost = ost_end;
				}
				else
				{
					/*
					* 其他节点 <name></name>
					*/

					// 输出 <
					nPos += OUTPUT_STR(pBuffer, nPos, nLen, L'<');

					// 对于XML协议节点,输出 ?
					if(pCurNode->m_type == et_xml)
					{
						nPos += OUTPUT_STR(pBuffer, nPos, nLen, L'?');
					}

					// 输出tag名称
					nPos += OUTPUT_STR(pBuffer, nPos, nLen, pCurNode->m_strName.c_str());

					// 输出属性名和值(统一使用双引号)
					for(iter_wstr2wstr_t iter = pCurNode->m_AttrList.begin(); iter != pCurNode->m_AttrList.end(); ++iter)
					{
						nPos += OUTPUT_STR(pBuffer, nPos, nLen, L' ');
						nPos += OUTPUT_STR(pBuffer, nPos, nLen, iter->first.c_str());
						nPos += OUTPUT_STR(pBuffer, nPos, nLen, L"=\"");
						nPos += OUTPUT_STR(pBuffer, nPos, nLen, iter->second.c_str());
						nPos += OUTPUT_STR(pBuffer, nPos, nLen, L'\"');
					}

					/*
					* 输出子节点
					*/
					if(pCurNode->m_pFirstChild)
					{
						// 输出 >
						nPos += OUTPUT_STR(pBuffer, nPos, nLen, L'>');

						// 把当前节点压栈,然后开始输出第一个子节点
						pCurNode->m_pOutputChild = pCurNode->m_pFirstChild;
						pCurNode->m_ost = ost_child;

						NodeStack.push(pCurNode);
						pCurNode = pCurNode->m_pOutputChild;
						pCurNode->m_ost = ost_begin;
					}
					else
					{
						// 节点带有结束标签,则先输出一个 '>' 然后转入结束标签输出状态.
						if(pCurNode->m_bHasEndTag)
						{
							nPos += OUTPUT_STR(pBuffer, nPos, nLen, L'>');
							pCurNode->m_ost = ost_endtag;
						}
						else
						{
							// 没有子节点也不输出结束标签,直接输出 /> 或者 ?> 后结束
							wchar_t ch = pCurNode->m_type == et_xml ? L'?' : L'/';
							nPos += OUTPUT_STR(pBuffer, nPos, nLen, ch);
							nPos += OUTPUT_STR(pBuffer, nPos, nLen, L'>');
							pCurNode->m_ost = ost_end;
						}
					}
				}
			}
			break;
		case ost_child:
			{
				// 一个子节点已经输出完毕,准备输出下一个子节点.
				pCurNode->m_pOutputChild = pCurNode->m_pOutputChild->m_pNextSibling;

				if(pCurNode->m_pOutputChild)
				{
					// 当前节点压栈
					pCurNode->m_ost = ost_child;
					NodeStack.push(pCurNode);

					// 输出下一个子节点
					pCurNode = pCurNode->m_pOutputChild;
					pCurNode->m_ost = ost_begin;
				}
				else
				{
					// 没有子节点了
					pCurNode->m_ost = ost_endtag;
				}
			}
			break;
		case ost_endtag:
			{
				// 输出结束标签 </name> 或者 <?name>
				wchar_t ch = pCurNode->m_type == et_xml ? L'?' : L'/';
				
				nPos += OUTPUT_STR(pBuffer, nPos, nLen, L'<');
				nPos += OUTPUT_STR(pBuffer, nPos, nLen, ch);
				nPos += OUTPUT_STR(pBuffer, nPos, nLen, pCurNode->m_strName.c_str());
				nPos += OUTPUT_STR(pBuffer, nPos, nLen, L'>');
				
				pCurNode->m_ost = ost_end;
			}
			break;
		case ost_end:
			{
				// 重置状态预备下一次输出
				pCurNode->m_ost = ost_begin;
				pCurNode->m_pOutputChild = NULL;

				// 恢复输出断点
				if(NodeStack.empty())
				{
					pCurNode = NULL;
				}
				else
				{
					pCurNode = NodeStack.top();
					NodeStack.pop();
				}
			}
			break;
		default: break;
		}
	}

	return nPos;
}

////////////////////////////////////////////////////////////////////
/*
* 简单支持 XPath
*/
class XPath
{
private:
	string m_strPath;
	XMLHANDLE m_hCurNode;
	XMLDocument *m_XmlDoc;
	std::list<XMLHANDLE> m_ResultsList;

	void Find();

public:
	XPath(XMLDocument *xmlDoc, XMLHANDLE hCur, const string &strPath);
	~XPath();

	size_t Count();
	XMLHANDLE First();
	XMLHANDLE Next();
	XMLHANDLE Last();
};

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

XMLDocument::XMLDocument()
	: m_hXmlRoot(NULL), m_hRoot(NULL)
{
}

XMLDocument::~XMLDocument()
{
	Destroy();
}

bool XMLDocument::Destroy()
{
	if(m_hRoot)
	{
		m_hRoot->DeleteTree();
		delete m_hRoot;
		m_hRoot = NULL;
	}

	if(m_hXmlRoot)
	{
		m_hXmlRoot->DeleteTree();
		delete m_hXmlRoot;
		m_hXmlRoot = NULL;
	}
	return true;
}

XMLHANDLE XMLDocument::Build(const char *pRoot, const char* pVersion, const char* pEncode)
{
	if(m_hXmlRoot) return NULL;

	m_hXmlRoot = new XMLNode(et_xml);
	SetName(m_hXmlRoot, "xml");
	SetAttrValue(m_hXmlRoot, "version", pVersion);
	SetAttrValue(m_hXmlRoot, "encoding", pEncode);

	m_hRoot = new XMLNode(et_normal);
	SetName(m_hRoot, pRoot);

	return m_hRoot;
}

bool XMLDocument::Load(const char* pszBuffer, int nLen, bool bKeepBlank)
{
	// 已经载入,需要先调用 Destroy()
	bool bRet = false;
	if(m_hXmlRoot) return bRet;

	// 如果是宽字符串, 进入Unicode Load入口.
	if(*((wchar_t*)pszBuffer) == (wchar_t)0xFEFF)
	{
		return Load((cwchar_t*)pszBuffer, nLen, bKeepBlank);
	}

	// 兼容微软的BOM表示(UTF-8标识),转化为UNICODE后进入 Unicode Load入口
	if(nLen > 3 && pszBuffer[0] == (char)0xEF && pszBuffer[1] == (char)0xBB && pszBuffer[2] == (char)0xBF) 
	{
		pszBuffer += 3; 
		nLen -= 3;
		int nWLen = OS_AToW("utf-8", pszBuffer, nLen, NULL, 0);
		if(nWLen <= 0)
		{
			return false;
		}
		wchar_t *pszWBuffer = new wchar_t[nWLen + 1];
		WSTR_ZERO(pszWBuffer, nWLen + 1);
		OS_AToW("utf-8", pszBuffer, nLen, pszWBuffer, nWLen);

		bRet = Load(pszWBuffer, nWLen, bKeepBlank);
		delete []pszWBuffer;
		return bRet;
	}

	// 定位第一个节点的结束位置,由于此时没有字符编码的信息
	// 默认编码是 utf-8
	int nFirstNodeEndPos = 0;
	while(nFirstNodeEndPos < nLen)
	{
		if(pszBuffer[nFirstNodeEndPos++] == '>') break;
	}

	if(nFirstNodeEndPos <= nLen)
	{
		cwchar_t *pwszEnd = NULL;
		
		// 把第一个 '>' 之前(包括'>')转化为宽字符,并调用分析函数
		int nFirstNodeLen = OS_AToW("utf-8", pszBuffer, nFirstNodeEndPos, NULL, 0);
		wchar_t *pFirstNodeBuf = new wchar_t[nFirstNodeLen + 1];
		WSTR_ZERO(pFirstNodeBuf, nFirstNodeLen + 1);
		OS_AToW("utf-8", pszBuffer, nFirstNodeLen, pFirstNodeBuf, nFirstNodeLen);

		XMLNode *pTmpNode = new XMLNode(et_none);
		if(!pTmpNode->LoadNode(pFirstNodeBuf, pwszEnd, bKeepBlank))
		{
			delete pTmpNode;
		}
		else
		{
			// 设置新的起始位置
			pszBuffer += nFirstNodeEndPos;
			nLen -= nFirstNodeEndPos;

			// 第一个节点载入成功,判断是不是 xml 协议节点
			if( GetType(pTmpNode) == et_xml && GetName(pTmpNode) == "xml" )
			{
				// 第一个节点是 xml 协议节点,则尝试载入根节点
				m_hXmlRoot = pTmpNode;
				
				// 根据 xml 协议节点中的编码设置,尝试载入根节点
				if(nLen <= 0)
				{
					// 载入失败,可以没有 xml 协议节点,但是不能没有根节点
				}
				else
				{
					string strEncode = GetAttrValue(m_hXmlRoot, "encoding");
					int nRootNodeLen = 0;
					wchar_t *pRootNodeBuf = NULL;
					
					nRootNodeLen = OS_AToW(strEncode.c_str(), pszBuffer, nLen, NULL, 0);
					pRootNodeBuf = new wchar_t[nRootNodeLen + 1];
					WSTR_ZERO(pRootNodeBuf, nRootNodeLen + 1);
					OS_AToW(strEncode.c_str(), pszBuffer, nLen, pRootNodeBuf, nRootNodeLen);
					
					pTmpNode = new XMLNode(et_none);
					if(!pTmpNode->LoadNode(pRootNodeBuf, pwszEnd, bKeepBlank))
					{
						delete pTmpNode;
					}
					else
					{
						m_hRoot = pTmpNode;
						if(!G_IsValidText(pwszEnd))
						{
							bRet = true;
						}
						else
						{
							// 载入失败,只能有一个根节点,并且根节点后不能有空白之外的其他字符.
						}
					}

					delete []pRootNodeBuf;
				}
			}
			else
			{
				// 第一个节点不是 xml 协议节点,认为它就是根节点,没有指定协议节点,则创建一个默认的.
				m_hRoot = pTmpNode;

				if(!G_IsValidText(pwszEnd) && !G_IsValidText(pszBuffer))
				{
					m_hXmlRoot = new XMLNode(et_xml);
					SetName(m_hXmlRoot, "xml");
					SetAttrValue(m_hXmlRoot, "version", "1.0");
					SetAttrValue(m_hXmlRoot, "encoding", "ISO-8859-1"); // 或者 gb2312 

					bRet = true;
				}
				else
				{
					// 载入失败,只能有一个根节点,并且根节点后不能有空白之外的其他字符.
				}
			}
		}

		delete []pFirstNodeBuf;
	}
	
	// 如果载入失败,重置 xml 树
	if(!bRet)
	{
		Destroy();
	}
	return bRet;
}

/*
* UNICODE 载入入口
* 忽略输入源中关于编码的设置
*/
bool XMLDocument::Load(cwchar_t *pwszBuffer, int nLen, bool bKeepBlank)
{
	bool bRet = false;
	if(m_hXmlRoot) return bRet;

	// 跳过 UNICODE 标记(Windows系统下的文件常常有这个标记)
	if(pwszBuffer[0] == (cwchar_t)0xFEFF)
	{
		++pwszBuffer;
		--nLen;
	}

	// 载入第一个节点
	cwchar_t *pwszEnd = NULL;
	XMLNode *pTmpNode = new XMLNode(et_none);
	if(pTmpNode->LoadNode(pwszBuffer, pwszEnd, bKeepBlank))
	{
		// 第一个节点载入成功
		if(GetType(pTmpNode) == et_xml && GetName(pTmpNode) == "xml")
		{
			// 第一个节点是 xml 协议节点,继续载入根节点
			m_hXmlRoot = pTmpNode;

			// 设置新的起始位置
			pwszBuffer = pwszEnd;
			pTmpNode = new XMLNode(et_none);
			if(pTmpNode->LoadNode(pwszBuffer, pwszEnd, bKeepBlank))
			{
				m_hRoot = pTmpNode;
				if(!G_IsValidText(pwszEnd))
				{
					bRet = true;
				}
				else
				{
					// 载入失败,只能有一个根节点,并且根节点后不能有空白之外的其他字符.
				}
			}
			else
			{
				delete pTmpNode;
			}
		}
		else
		{
			// 第一个节点不是 xml 协议节点,则认为是根节点
			m_hRoot = pTmpNode;
			if(!G_IsValidText(pwszEnd))
			{
				// 创建一个默认的 xml 协议节点
				m_hXmlRoot = new XMLNode(et_xml);
				SetName(m_hXmlRoot, "xml");
				SetAttrValue(m_hXmlRoot, "version", "1.0");
				SetAttrValue(m_hXmlRoot, "encoding", "ISO-8859-1"); // 或者 gb2312 
				bRet = true;
			}
			else
			{
				// 载入失败,根节点后面不允许有非空白字符
			}
		}
	}
	else
	{
		delete pTmpNode;
	}
	
	if(!bRet)
	{
		Destroy();
	}
	return bRet;
}

bool XMLDocument::Load(const char* pszFileName, bool bKeepBlank)
{
	bool bRet = false;
	FILE *pFile = NULL;
	pFile = fopen(pszFileName, "rb");
	if(pFile)
	{
		fseek(pFile, 0, SEEK_END);
		long lLen = ftell(pFile);
		fseek(pFile, 0, SEEK_SET);

		byte *pBuffer = new byte[lLen + 3];	// 确保即使是UNICODE文件,读出的字符串以0结尾.
		if(pBuffer)
		{
			memset(pBuffer, 0, lLen + 3);
			fread(pBuffer, 1, lLen, pFile);

			bRet = Load((char*)pBuffer, lLen, bKeepBlank);
			delete []pBuffer;
		}

		fclose(pFile);
	}
	return bRet;
}

XMLHANDLE XMLDocument::AppendNode(XMLHANDLE hParent, const char *pBuffer, int nLen, bool bKeepBlank)
{
	// 把输入源转换为 UNICODE 宽字符串
	int wLen = OS_AToW(NULL, pBuffer, nLen, NULL, 0);
	wchar_t *pwBuffer = new wchar_t[wLen + 1];
	WSTR_ZERO(pwBuffer, wLen + 1);
	OS_AToW(NULL, pBuffer, nLen, pwBuffer, wLen);

	cwchar_t *pwszEnd = NULL;
	XMLHANDLE hNewNode = new XMLNode(et_none);
	if(hNewNode->LoadNode(pwBuffer, pwszEnd, bKeepBlank))
	{
		// 不必检查节点后面是否还有字符
		if(hParent)
		{
			hParent->LinkChild(hNewNode);
		}
		else
		{
			m_hRoot->LinkChild(hNewNode);
		}
	}
	else
	{
		delete hNewNode;
		hNewNode = NULL;
	}
	delete []pwBuffer;
	return hNewNode;
}

// 输出XML节点的内容(不包括0)
// 返回实际写入/需要的字节个数. 0表示失败.
int XMLDocument::GetString(XMLHANDLE hXml, char *pBuffer, int nLen)
{
	int nPos = 0;
	if(hXml)
	{
		// 获取单个节点的内容时,总是输出成ANSI串
		int nNeedSize = hXml->GetNode(NULL, 0);

		wchar_t *pwszOut = new wchar_t[nNeedSize + 1];
		WSTR_ZERO(pwszOut, nNeedSize + 1);
		hXml->GetNode(pwszOut, nNeedSize);

		nPos = OS_WtoA(NULL, pwszOut, nNeedSize, pBuffer, nLen);
		delete []pwszOut;
	}
	else if(m_hRoot == NULL)
	{
		// return 0
	}
	else
	{
		assert(m_hRoot && m_hXmlRoot);

		/*
		* XMLNode 只能输出宽字符串, 由于在实际调用 OS_WtoA 之前无法知道 n 长度的宽字符串转化为A字符串后的长度
		* 只能进行实际操作.
		*/

		// 计算得到实际需要的宽字符串长度
		int wLen = m_hXmlRoot->GetNode(NULL, 0);
		wLen += 2; // 换行符,总是在 xml 协议节点末尾添加一个换行符
		wLen += m_hRoot->GetNode(NULL, 0);

		// 分配缓冲去,进行实际输出
		int wPos = 0;
		wchar_t *pwBuf = new wchar_t[wLen + 1];
		WSTR_ZERO(pwBuf, wLen + 1);
		wPos = m_hXmlRoot->GetNode(pwBuf, wLen);
		pwBuf[wPos++] = L'\r';
		pwBuf[wPos++] = L'\n';
		wPos += m_hRoot->GetNode(pwBuf + wPos, wLen - wPos);

		// 转换为指定的编码
		string strEncode = GetAttrValue(m_hXmlRoot, "encoding");
		nPos = OS_WtoA(strEncode.c_str(), pwBuf, wPos, pBuffer, nLen);


		// 删除缓冲区
		delete []pwBuf;
	}
	return nPos;
}


bool XMLDocument::Save(const char* pszFileName)
{
	bool bRet = false;
	int nOut = GetString(NULL, NULL, 0);

	char *pOut = new char[nOut];
	if(pOut)
	{
		GetString(NULL, pOut, nOut);

		FILE *pFile = fopen(pszFileName, "wb");
		if(pFile)
		{
			fwrite(pOut, 1, nOut, pFile);
			fclose(pFile);
			bRet = true;
		}

		delete []pOut;
	}
	return bRet;
}

XMLHANDLE XMLDocument::AppendNode(XMLHANDLE hParent, const char* pName, xmlnode_type type /* = et_normal */)
{
	if(!m_hXmlRoot) return NULL;

	XMLNode *pNode = new XMLNode(type);
	if(pName) pNode->m_strName = G_A2W(pName);
	if(hParent == NULL)
	{
		m_hRoot->LinkChild(pNode);
	}
	else
	{
		hParent->LinkChild(pNode);
	}
	return pNode;
}

bool XMLDocument::SetAttrValue(XMLHANDLE hXml, const char *pszAttr, const char *pszValue)
{
	if(hXml == NULL || pszAttr == NULL) return false;
	wstring wstrAttr = G_A2W(pszAttr);
	wstring wstrValue = G_A2W(pszValue);

	for(iter_wstr2wstr_t iter = hXml->m_AttrList.begin(); iter != hXml->m_AttrList.end(); ++iter)
	{
		if(wstrAttr == iter->first)
		{
			iter->second = wstrValue;
			return true;
		}
	}
	
	wstr2wstr_t attr;
	attr.first = wstrAttr;
	attr.second = wstrValue;
	hXml->m_AttrList.push_back(attr);
	return true;
}

string XMLDocument::GetAttrValue(XMLHANDLE hXml, const char *pszAttr)
{
	if(hXml)
	{
		iter_wstr2wstr_t iter;
		wstring wstrAttr = G_A2W(pszAttr);
		for(iter = hXml->m_AttrList.begin(); iter != hXml->m_AttrList.end(); ++iter)
		{
			if(iter->first == wstrAttr) return G_W2A(iter->second.c_str());
		}
	}

	return "";
}

XMLHANDLE XMLDocument::GetRootNode()
{
	return m_hRoot;
}

xmlnode_type XMLDocument::GetType(XMLHANDLE hXml)
{
	return hXml->m_type;
}

bool XMLDocument::GetAttrList(XMLHANDLE hXml, list_str2str* pList)
{
	if(hXml && pList)
	{
		for(iter_wstr2wstr_t iter = hXml->m_AttrList.begin(); iter != hXml->m_AttrList.end(); ++iter)
		{
			pList->push_back(str2str(G_W2A(iter->first.c_str()), G_W2A(iter->second.c_str())));
		}
		return true;
	}
	return false;
}

bool XMLDocument::SetName(XMLHANDLE hXml, const char *pszName)
{
	hXml->m_strName = G_A2W(pszName);
	return true;
}

string XMLDocument::GetName(XMLHANDLE hXml)
{
	if(hXml) return G_W2A(hXml->m_strName.c_str());
	else return "";
}

bool XMLDocument::DeleteNode(XMLHANDLE hXml)
{
	if(!hXml || hXml == m_hRoot || hXml == m_hXmlRoot)
	{
		// 根节点不能被删除
		return false;
	}
	else
	{
		hXml->Unlink();
		hXml->DeleteTree();
		delete hXml;
		return true;
	}
}

bool XMLDocument::DeleteAllChildren(XMLHANDLE hXml)
{
	if(!hXml)
	{
		return false;
	}
	else
	{
		hXml->DeleteTree();
		return true;
	}
}

XMLHANDLE XMLDocument::Parent(XMLHANDLE hXml)
{
	if(hXml) return hXml->m_pParent;
	else return NULL;
}

XMLHANDLE XMLDocument::FirstChild(XMLHANDLE hXml)
{
	if(hXml) return hXml->m_pFirstChild;
	else return NULL;
}

XMLHANDLE XMLDocument::NextSibling(XMLHANDLE hXml)
{
	if(hXml) return hXml->m_pNextSibling;
	else return NULL;
}

XMLHANDLE XMLDocument::PrevSibling(XMLHANDLE hXml)
{
	if(hXml) return hXml->m_pPrevSibling;
	else return NULL;
}

XMLHANDLE XMLDocument::GetChildByName(XMLHANDLE hXml, const char *pszName)
{
	XMLHANDLE hChild = FirstChild(hXml);
	while(hChild)
	{
		if(GetName(hChild) == pszName) break;
		hChild = NextSibling(hChild);
	}

	return hChild;
}

XMLHANDLE XMLDocument::GetChildByAttr(XMLHANDLE hXml, const char *pszName, const char *pszAttr, const char *pszAttrValue)
{
	XMLHANDLE hChild = FirstChild(hXml);
	while(hChild)
	{
		if(GetName(hChild) == pszName && GetAttrValue(hChild, pszAttr) == pszAttrValue)
		{
			break;
		}
		hChild = NextSibling(hChild);
	}

	return hChild;
}

string XMLDocument::GetText(XMLHANDLE hXml)
{
	if(hXml) return G_W2A(hXml->m_strText.c_str());
	else return "";
}

bool XMLDocument::SetText(XMLHANDLE hXml, const char *pszText)
{
	if(hXml) hXml->m_strText = G_A2W(pszText);
	return hXml != NULL;
}

XMLHANDLE XMLDocument::GetXmlRoot()
{
	return m_hXmlRoot;
}

//XMLHANDLE XMLDocument::AddContent(XMLHANDLE hXml, const char* pszContent, bool bCdata)
//{
//	XMLHANDLE hRet = NULL;
//	if(hXml)
//	{
//		hRet = AppendNode(hXml, NULL, bCdata ? et_cdata : et_text);
//		SetText(hRet, pszContent);
//	}
//
//	return hRet;
//}

XMLHANDLE XMLDocument::SetContent(XMLHANDLE hXml, const char *pszContent)
{
	if(hXml == NULL) return NULL;

	XMLHANDLE hChild = FirstChild(hXml);
	while(hChild)
	{
		if(et_text == GetType(hChild))
		{
			break;
		}
		hChild = NextSibling(hChild);
	}

	if(!hChild) hChild = AppendNode(hXml, NULL, et_text);
	SetText(hChild, pszContent);
	return hChild;
}

std::string XMLDocument::GetContent(XMLHANDLE hXml, XMLHANDLE* phContent)
{
	if(hXml == NULL) return "";
	
	XMLHANDLE hChild = FirstChild(hXml);
	while(hChild)
	{
		if(et_text == GetType(hChild))
		{
			if(phContent != NULL)
			{
				*phContent = hChild;
			}
			return GetText(hChild);
		}

		hChild = NextSibling(hChild);
	}

	return "";
}

/*
* 最简单的那种定位
*/
XMLHANDLE XMLDocument::GetNode(const char *pszPath, bool bCreateNew)
{
	XMLHANDLE hNode = NULL;

	// 按照'/'分隔符定位节点
	std::vector<std::string> vecPaths;
	if(0 == G_SplitStrings(pszPath, vecPaths, '/'))
	{
		return NULL;
	}

	// 定位根节点(路径的第一个节点必定是根节点)
	std::vector<std::string>::iterator iter = vecPaths.begin();
	if(GetName(m_hRoot) != (*iter))
	{
		return NULL;
	}
	
	hNode = m_hRoot;
	for(++iter; iter != vecPaths.end(); ++iter)
	{
		XMLHANDLE hChild = GetChildByName(hNode, (*iter).c_str());
		if(hChild == NULL)
		{
			if(bCreateNew)
			{
				hNode = AppendNode(hNode, (*iter).c_str(), et_normal);
			}
			else
			{
				hNode = NULL;
				break;
			}
		}
		else
		{
			hNode = hChild;
		}
	}

	return hNode;
}

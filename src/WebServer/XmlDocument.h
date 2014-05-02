// XmlProcess.h: interface for the XMLDocument class.
//
// 版权声明: 可以修改或者用于商业用途,但请保留原作者的相关信息.
// 作者: 阙荣文 (querw) / Que's C++ Studio
// 日期: 2006四月
//
// Update list
//
// 重新整理代码,主要修改如下:
//
// 1. 添加 XPath 最简单的支持,可以直接定位. GetNode(), GetChildByPath(), AppendNode(path)
//
// 2. 几个地方的递归恐怕会溢出 1. XmlNode::~XmlNode() 递归删除所有子节点; 2. XmlNode::LoadNode() 递归创建所有的子节点; 3. GetNode()
//
// 3. 添加 et_positionholder 类型的节点,用于保留原始xml源中的空白字符,即便如此,同样也会造成一些困扰,因为占位节点是以子节点插入在父节点的队列中
//    所以,浏览子节点的时候要注意过滤此类节点. 另外, xml 协议节点之后,根节点之前的空白字符和根节点之后的空白字符总是被舍去,因为没有任何意义.
//
// 4. 现在一个 xml 文档只允许有1个根节点
//
// 5. 应该说内存树(即xml文件被读入内存保存的一颗多叉树)使用utf-8编码是最优的,但是考虑到Windows平台下使用较多,而Windows平台直接用UTF-16
//	  保存宽字符串,所以没有去修改. 不过 XMLNode 的分析函数 LoadNode 很容易被改造为可以处理 char 型的字符串.
//
// - 不足: 最大的问题还是使用了太多的内存,尤其是载入时, 读入文件需要一个缓冲区, 转化为宽字符需要一个缓冲区, 载入为内存数又需要内存.
//
//
// 2012-4-11 by 阙荣文
//
//////////////////////////////////////////////////////////////////////

#if !defined(_XMLDOCUMENT_H_)
#define _XMLDOCUMENT_H_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#pragma warning(disable : 4786)
#include <string>
#include <list>

enum xmlnode_type
{
	et_none,		// 创建时不确定,会根据内容决定
	et_xml,			// <?xml ...?>
	et_comment,		// <!-- ... -->
	et_normal,		// <tag />
	et_text,		// content text
	et_cdata,		// <![CDATA[ ... ]]>
	et_positionholder, // 空白占位符,缩进格式的 TAB 等.
};

typedef const wchar_t cwchar_t;
typedef std::pair<std::string, std::string> str2str;
typedef std::list<str2str> list_str2str;
typedef list_str2str::iterator iter_str2str;

class XMLNode;
typedef XMLNode* XMLHANDLE;
typedef std::list<XMLNode*> list_nodeptr;
typedef list_nodeptr::iterator iter_nodeptr;

class XMLDocument
{
protected:
	/*
	* 两个指针总是同时有效同时失效
	*/
	XMLHANDLE m_hXmlRoot; // xml 协议节点,典型的 <?xml version="1.0" encoding="utf-8"?>
	XMLHANDLE m_hRoot; // 根节点,一个 xml 文档只能有一个根节点.

private:
	/*
	* 禁止复制
	*/
	XMLDocument(const XMLDocument&);
	XMLDocument& operator = (const XMLDocument&);

public:
	XMLDocument();
	virtual ~XMLDocument();

	XMLHANDLE Build(const char *pRoot, const char* pVersion, const char* pEncode); // 创建 xml 树,返回根节点.
	bool Load(const char* pszBuffer, int nLen, bool bKeepBlank = false);	// 从完整的XML文本流中读入(编码方式在字符串中指定)
	bool Load(cwchar_t *pwszBuffer, int nLen, bool bKeepBlank = false);		// 从一个宽字符串中读入整棵树
	bool Load(const char* pszFileName, bool bKeepBlank = false);		// 从一个文件中读入整棵树
	int GetString(XMLHANDLE hXml, char *pBuffer, int nLen); // hXml = NULL 表示返回整棵树(编码方式在跟节点中指定) 当hXml!=NULL时,返回ANSI串
	bool Save(const char*pszFileName);	// 保存到文件
	bool Destroy();

	XMLHANDLE GetXmlRoot();	// 返回XML协议节点
	XMLHANDLE GetRootNode();	// 返回根节点
	XMLHANDLE GetNode(const char *pszPath, bool bCreateNew = false); // 返回 XPath 表示的节点
	XMLHANDLE AppendNode(XMLHANDLE hParent, const char* pName, xmlnode_type type = et_normal);
	XMLHANDLE AppendNode(XMLHANDLE hParent, const char *pBuffer, int nLen, bool bKeepBlank = false); // 新节点从一个ANSI串构建
	bool DeleteNode(XMLHANDLE hXml);

	std::string GetName(XMLHANDLE hXml);
	bool SetName(XMLHANDLE hXml, const char *pszName);
	std::string GetAttrValue(XMLHANDLE hXml, const char *pszAttr);
	bool SetAttrValue(XMLHANDLE hXml, const char *pszAttr, const char *pszValue);
	bool GetAttrList(XMLHANDLE hXml, list_str2str* pList);
	xmlnode_type GetType(XMLHANDLE hXml);
	std::string GetText(XMLHANDLE hXml);
	bool SetText(XMLHANDLE hXml, const char *pszText); // 获取当前节点的 m_strText 值, 类型为 ct_comment , ct_cdata, ct_text 
	std::string GetContent(XMLHANDLE hXml, XMLHANDLE *phXml = NULL); // 总是获取第一个类型为 ct_text 的子节点的内容 <tag>content</tag>
	XMLHANDLE SetContent(XMLHANDLE hXml, const char *pszContent);
	
	XMLHANDLE FirstChild(XMLHANDLE hXml);
	XMLHANDLE NextSibling(XMLHANDLE hXml);
	XMLHANDLE PrevSibling(XMLHANDLE hXml);
	XMLHANDLE Parent(XMLHANDLE hXml);

	XMLHANDLE GetChildByName(XMLHANDLE hXml, const char *pszName);
	XMLHANDLE GetChildByAttr(XMLHANDLE hXml, const char *pszName, const char *pszAttr, const char *pszAttrValue);
	XMLHANDLE GetChildByPath(XMLHANDLE hXml, const char *pszPath); // 根据相对路径获得子节点
	bool DeleteAllChildren(XMLHANDLE hXml);
};

#endif // !defined(_XMLDOCUMENT_H_)

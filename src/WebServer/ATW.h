/* Copyright (C) 2011 阙荣文
 *
 * 这是一个开源免费软件,您可以自由的修改和发布.
 * 禁止用作商业用途.
 *
 * 联系原作者: querw@sina.com 
*/

/*

1. 实现USC的压缩,Unicode <-> UTF-8.
2. 实现多字节码 <-> Unicode

对于Unicode字符串,用 wstring 存储.
对于非Unicode字符串用 string 存储. ANSI, GB2312 都一样
对于UTF-8串,也用 string 存储, UTF-8的编码串中不会有null出现.
*/

/*
 不要使用ATL 中的 USES_CONVERSION; A2W, A2T, W2A 等的宏, 由于这些宏都调用 alloca() 函数在函数栈中分配内存.
 虽然非常方便,函数返回后自动回收, 但是有溢出的危险, 函数栈只有 1M 的大小.

 以下的函数使用的空间都是在堆中分配的,比较安全.
*/

#pragma once
#include <string>
#if defined(_WIN32) || defined(WIN32)
#include "Windows.h"
#endif

#if defined(_UNICODE) || defined(UNICODE)
#define TtoA WtoA
#define AtoT AtoW
#define WtoT(a) (a)
#define TtoW(a) (a)
typedef std::wstring _tstring;
#else
#define TtoA(a) (a)
#define AtoT(a) (a)
#define WtoT WtoA
#define TtoW AtoW
typedef std::string _tstring;
#endif

std::string WtoA(const wchar_t* pwszSrc);
std::string WtoA(const std::wstring &strSrc);

std::wstring AtoW(const char* pszSrc);
std::wstring AtoW(const std::string &strSrc);

std::string WtoUTF8(const wchar_t* pwszSrc);
std::string WtoUTF8(const std::wstring &strSrc);

std::wstring UTF8toW(const char* pszSrc);
std::wstring UTF8toW(const std::string &strSr);

std::string AtoUTF8(const char* src);
std::string AtoUTF8(const std::string &src);

std::string UTF8toA(const char* src);
std::string UTF8toA(const std::string &src);

// 检测一个以 null 结尾的字符串是否是UTF-8, 如果返回0, 也只表示这个串刚好符合UTF8的编码规则.
// 返回值说明: 
// 1 -> 输入字符串符合UTF-8编码规则
// -1 -> 检测到非法的UTF-8编码首字节
// -2 -> 检测到非法的UTF-8字节编码的后续字节.
int IsTextUTF8(const char* pszSrc); 
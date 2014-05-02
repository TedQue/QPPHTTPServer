/* Copyright (C) 2011 阙荣文
 *
 * 这是一个开源免费软件,您可以自由的修改和发布.
 * 禁止用作商业用途.
 *
 * 联系原作者: querw@sina.com 
*/

#include "stdafx.h"
#include <time.h>
#include "HTTPDef.h"

static char month[][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static char week[][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static char hex[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

const char* g_HTTP_Content_NotFound = "404 Not Found - Q++ HTTP Server";
const char* g_HTTP_Bad_Request = "400 Bad Request - Q++ HTTP Server";
const char* g_HTTP_Bad_Method = "405 Method Not Allowed - Q++ HTTP Server";
const char* g_HTTP_Server_Error = "500 Oops, server error - Q++ HTTP Server";
const char* g_HTTP_Forbidden = "403 Forbidden - Q++ HTTP Server";
const char* g_HTTP_Server_Busy = "503 Service Unavailable, try again later - Q++ HTTP Server";

std::string format_http_date(__int64* ltime)
{
	struct tm t;
	if(ltime != NULL)
	{
		_gmtime64_s(&t, ltime);
	}
	else
	{
		//  传入空指针,则取当前时间.
		__int64 ltime_cur;
		_time64( &ltime_cur );
		_gmtime64_s(&t, &ltime_cur);
	}

	char szTime[100] = {0};

	// 格式化邮件时间 - Sun, 24 Aug 2008 22:43:45 GMT
	sprintf(szTime, "%s, %d %s %d %d:%d:%d GMT", 
		week[t.tm_wday], t.tm_mday, month[t.tm_mon], 
		t.tm_year + 1900, t.tm_hour, t.tm_min, t.tm_sec);

	return szTime;
}

std::string to_hex(const unsigned char* pData, int nSize)
{
	int nStrSize = nSize * 2 + 1;
	char* pStr = new char[nStrSize];
	memset(pStr, 0, nStrSize);

	int nPos = 0;
	for(int i = 0; i < nSize; ++i)
	{
		pStr[nPos] = hex[pData[i] >> 4];
		pStr[nPos + 1] = hex[pData[i] & 0x0F];
		nPos += 2;
	}

	std::string str(pStr);
	delete[] pStr;

	return str;
}

std::string decode_url(const std::string& inputStr)
{
	// 转换为ANSI编码
	const std::string &astr = inputStr;
	std::string destStr("");

	// 扫描,得到一个UTF8字符串
	bool isEncoded = false;
	for(std::string::size_type idx = 0; idx < astr.size(); ++idx)
	{
		char ch = astr[idx];
		if(ch == '%')
		{
			isEncoded = true;
			if( idx + 1 < astr.size() && idx + 2 < astr.size() )
			{
				char orgValue[5] = {0}, *stopPos = NULL;
				orgValue[0] = '0';
				orgValue[1] = 'x';
				orgValue[2] = astr[idx + 1];
				orgValue[3] = astr[idx + 2];
				orgValue[4] = 0;
				ch = static_cast<char> (strtol(orgValue, &stopPos, 16));

				idx += 2;
			}
			else
			{
				// 格式错误
				break;
			}
		}

		destStr.push_back(ch);
	}

	if(isEncoded) return UTF8toA(destStr);
	else return inputStr;
}

bool map_method(HTTP_METHOD md, char *str)
{
	switch(md)
	{
	case METHOD_HEAD: strcpy(str, "HEAD"); break;
	case METHOD_PUT: strcpy(str, "PUT"); break;
	case METHOD_POST: strcpy(str, "POST"); break;
	case METHOD_GET: strcpy(str, "GET"); break;
	case METHOD_TRACE: strcpy(str, "TRACE"); break;
	case METHOD_CONNECT: strcpy(str, "CONNECT"); break;
	case METHOD_DELETE: strcpy(str, "DELETE"); break;
	default: return false;
	}
	return true;
}


bool is_end(const byte *data, size_t len)
{
	if( len < 4 ) return false;
	else return strncmp(reinterpret_cast<const char*>(data) + len - 4, "\r\n\r\n", 4) == 0;
}

std::string get_field(const char* buf, const char* key)
{
	std::string strKey(key);
	strKey += ": ";
	std::string strValue("");

	// 找到字段的开始:起始位置或者是新一行的起始位置
	const char* pszStart = strstr(buf, strKey.c_str());
	if(pszStart == NULL || (pszStart != buf && *(pszStart - 1) != '\n')) return strValue;
	pszStart += strKey.size();

	// 找到字段结束
	const char* pszEnd = strstr(pszStart, "\r\n");
	if(pszEnd == NULL) return strValue;

	strValue.assign(pszStart, pszEnd - pszStart);
	return strValue;
}


/*
* 截取文件名的扩展名,如 html,php等.
*/
void get_file_ext(const std::string &fileName, std::string &ext)
{
	std::string::size_type st = 0, ed = 0;

	/* 找到最后一个'/'*/
	std::string::size_type pos = fileName.rfind('/');
	if(pos != std::string::npos)
	{
		st = pos + 1;
	}
	else
	{
		st = 0;
	}

	/* 在上一步的基础上,找到第一个'.'*/
	pos = fileName.find('.', st);
	if(pos != std::string::npos)
	{
		st = pos + 1;
	}
	else
	{
		st = fileName.size();
	}

	/* 直到文件名结尾 */
	ed = fileName.size();

	if(st < ed)
	{
		ext = fileName.substr(st, ed - st);
	}
	else
	{
		ext = "";
	}
}

/*
* 比较扩展是否匹配,扩展名列表的格式如: "php,htm,html;js"
* 如果扩展名列表为 "*" 表示匹配所有.
*/
bool match_file_ext(const std::string &ext, const std::string &extList)
{
	if(ext.empty()) return false;
	if( extList == "*" ) return true;

	/* 是否存在 */
	std::string::size_type pos = extList.find(ext);
	if( pos == extList.npos )
	{
		return false;
	}

	/* 字符串开头或者在",;"后面*/
	if(pos != 0)
	{
		if(extList.at(pos - 1) == ',' || extList.at(pos - 1) == ';')
		{
		}
		else
		{
			return false;
		}
	}

	/* 字符串结尾或者后面还有 ",;" */
	if( pos + ext.size() != extList.size() )
	{
		pos += ext.size();
		if(extList.at(pos) == ',' || extList.at(pos) == ';')
		{
		}
		else
		{
			return false;
		}
	}

	return true;
}

/*
* 获得 GetLastError() 的描述字符串.
*/
std::string get_last_error(DWORD errCode /* = 0 */)
{
	std::string err("");
	if( errCode == 0 ) errCode = GetLastError();
	LPTSTR lpBuffer = NULL;
	if( 0 == FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 
		NULL,
		errCode, 
		0,  
		(LPTSTR)&lpBuffer, 
		0, 
		NULL) )
	{
		char tmp[100] = {0};
		sprintf(tmp, "{未定义错误描述(%d)}", errCode);
		err = tmp;
	}
	else
	{
		err = TtoA(lpBuffer);
		LocalFree(lpBuffer);
	}
	return err;
}

/*
* 分隔字符串
*/
size_t split_strings(const std::string &str, str_vec_t &vec, const std::string &sp)
{
	std::string srcString(str);
	srcString += sp; // 额外一个分隔符,方便循环处理.
	std::string::size_type st = 0;
	std::string::size_type stNext = 0;
	while( (stNext = srcString.find(sp, st)) != std::string::npos )
	{
		if(stNext > st)
		{
			vec.push_back(srcString.substr(st, stNext - st));
		}

		// next
		st = stNext + sp.size();
	}
	return vec.size();
}

/*
* 获取本机的 IP 地址
*/
bool get_ip_address(std::string& str)
{
	char hostName[MAX_PATH] = {0};
	if(gethostname(hostName, MAX_PATH))
	{
		return FALSE;
	}
	else
	{
		addrinfo hints, *res, *nextRes;
		memset(&hints, 0, sizeof(addrinfo));
		res = NULL;
		nextRes = NULL;
		hints.ai_family = AF_INET;

		if(getaddrinfo(hostName, NULL, &hints, &res))
		{
			return false;
		}
		else
		{
			str = "";
			nextRes = res;
			while(nextRes)
			{
				in_addr inAddr = ((sockaddr_in*)(nextRes->ai_addr))->sin_addr;
				str += inet_ntoa(inAddr);
				str += "/";

				nextRes = nextRes->ai_next;
			}
			str.pop_back();

			freeaddrinfo(res);
		}
	}

	return true;
}

std::string format_size(__int64 bytes)
{
	// 计算以发送数据的长度
	char buf[100] = {0};
	if(bytes >= G_BYTES)
	{
		sprintf(buf, "%.2fGB",  bytes * 1.0 / G_BYTES);
	}
	else if(bytes >= M_BYTES)
	{
		sprintf(buf, "%.2fMB", bytes * 1.0 / M_BYTES);
	}
	else if(bytes >= K_BYTES)
	{
		sprintf(buf, "%.2fKB", bytes * 1.0 / K_BYTES);
	}
	else
	{
		sprintf(buf, "%lldBytes", bytes);
	}
	return std::string(buf);
}


std::string format_speed(__int64 bytes, unsigned int timeUsed)
{
	// 计算平均数据
	char buf[100] = {0};

	if(timeUsed <= 0)
	{
		strcpy(buf, "---");
	}
	else
	{
		double llSpeed = bytes * 1.0 / timeUsed * 1000;
		if(llSpeed >= G_BYTES)
		{
			sprintf(buf, "%.2fGB/s", llSpeed * 1.0 / G_BYTES);
		}
		else if(llSpeed >= M_BYTES)
		{
			sprintf(buf, "%.2fMB/s", llSpeed * 1.0 / M_BYTES);
		}
		else if(llSpeed >= K_BYTES)
		{
			sprintf(buf, "%.2fKB/s", llSpeed * 1.0 / K_BYTES);
		}
		else
		{
			sprintf(buf, "%.2fB/s", llSpeed);
		}
	}

	return std::string(buf);
}


#include "StdAfx.h"
#include "HTTPResponseHeader.h"


HTTPResponseHeader::HTTPResponseHeader()
	: _resCode(SC_UNKNOWN)
{
}


HTTPResponseHeader::~HTTPResponseHeader()
{
}

std::string HTTPResponseHeader::getFirstLine()
{
	std::string str = "HTTP/1.1 ";
	switch (_resCode)
	{
	case SC_OK:
		{
			str += "200 OK";
			break;
		};
	case SC_NOCONTENT:
		{
			str += "204 No Content";
			break;
		};
	case SC_PARTIAL:
		{
			str += "206 Partial Content";
			break;
		};
	case SC_BADREQUEST:
		{
			str += "400 Bad Request";
			break;
		};
	case SC_OBJMOVED:
		{
			str += "302 Moved Temporarily";
			break;
		}
	case SC_NOTFOUND:
		{
			str += "404 Not Found";
			break;
		}
	case SC_BADMETHOD:
		{
			str += "405 Method Not Allowed";
			break;
		};
	default:
		str += "500 Internal Server Error";
	};

	str += "\r\n";
	return str;
}

int HTTPResponseHeader::setResponseCode(int resCode)
{
	int oldCode = _resCode;
	_resCode = resCode;
	return oldCode;
}

HTTPResponseHeader::fields_t::iterator HTTPResponseHeader::find(const std::string &name)
{
	fields_t::iterator iter = _headers.begin();
	for(; iter != _headers.end(); ++iter)
	{
		if(iter->first == name) break;
	}

	return iter;
}

bool HTTPResponseHeader::add(const std::string &name, const std::string &val)
{
	/*
	* 先查找,如果不存在在添加,如果存在则更新
	*/
	fields_t::iterator findRet = find(name);
	if(findRet == _headers.end())
	{
		_headers.push_back(std::make_pair(name, val));
	}
	else
	{
		findRet->second = val;
	}
	return true;
}

bool HTTPResponseHeader::add(const std::string &fields)
{
	// 按换行符分隔
	str_vec_t lns;
	split_strings(fields, lns, "\r\n");

	for(str_vec_t::iterator iter = lns.begin(); iter != lns.end(); ++iter)
	{
		// 同一行内 "name: value"
		std::string::size_type pos = iter->find(": ");
		if(std::string::npos != pos)
		{
			std::string name = iter->substr(0, pos);
			std::string val = iter->substr(pos + 2, iter->size() - pos - 2);
			add(name.c_str(), val.c_str());
		}
	}

	return true;
}

bool HTTPResponseHeader::remove(const std::string &name)
{
	fields_t::iterator findRet = find(name);
	if(findRet == _headers.end())
	{
		return false;
	}
	else
	{
		_headers.erase(findRet);
		return true;
	}
}

bool HTTPResponseHeader::getField(const std::string &name, std::string &val)
{
	fields_t::iterator ret = find(name);
	if( ret == _headers.end())
	{
		return false;
	}
	else
	{
		val = ret->second;
		return true;
	}
}

bool HTTPResponseHeader::format()
{
	// 清空缓存
	_buf.trunc();

	// 输出第一行
	write(getFirstLine());

	// 把关联数组内的所有 key - value 写入缓冲区
	for(fields_t::iterator iter = _headers.begin(); iter != _headers.end(); ++iter)
	{
		write(iter->first);
		write(": ");
		write(iter->second);
		write("\r\n");
	}

	// 写入一个空行表示结束
	write("\r\n");
	return true;
}

size_t HTTPResponseHeader::write(const std::string &str)
{
	return _buf.write(str.c_str(), str.size());
}

size_t HTTPResponseHeader::addDefaultFields()
{
	// Date
	add("Date", format_http_date(NULL));

	// HTTP Server
	add("Server", SERVER_SOFTWARE);

	return 2;
}

//bool HTTPResponseHeader::writeDefaultFields(HTTP_METHOD md, int svrCode)
//{
//	// 第一行
//	writeHeader(getFirstLine(svrCode).c_str());
//
//	// Date
//	writeHeader("Date: ");
//	writeHeader(format_http_date(NULL).c_str());
//	writeHeader("\r\n");
//
//	if(svrCode == SC_BADMETHOD)
//	{
//		writeHeader("Allow: GET, HEAD, POST, PUT\r\n");
//	}
//
//	// HTTP Server
//	writeHeader("Server: ");
//	writeHeader(SERVER_SOFTWARE);
//	writeHeader("\r\n");
//
//	return true;
//}

size_t HTTPResponseHeader::read(byte *buf, size_t len)
{
	return _buf.read(buf, len);
}

bool HTTPResponseHeader::eof()
{
	return _buf.eof();
}

void HTTPResponseHeader::reset()
{
	_headers.clear();
	_buf.trunc();
	_resCode = SC_UNKNOWN;
}

std::string HTTPResponseHeader::getHeader()
{
	return std::string(reinterpret_cast<char*>(_buf.buffer()), _buf.fsize());
}
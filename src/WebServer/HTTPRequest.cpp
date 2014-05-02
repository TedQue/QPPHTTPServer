/* Copyright (C) 2011 阙荣文
 *
 * 这是一个开源免费软件,您可以自由的修改和发布.
 * 禁止用作商业用途.
 *
 * 联系原作者: querw@sina.com 
*/

#include "StdAfx.h"
#include "HTTPRequest.h"

HTTPRequest::HTTPRequest(IHTTPServer *server, IOCPNetwork *network) :
	_hrt(true), _header(1024, MAX_REQUESTHEADERSIZE), _postFile(NULL), _isHeaderRecved(false), _bytesRecv(0),
	_server(server), _network(network), _sockBuf(NULL), _sockBufLen(0), _connId(NULL), _postData(NULL),
	_postFileName(""), _contentLength(0), _startTime(0)
{
}

HTTPRequest::~HTTPRequest()
{
}

size_t HTTPRequest::push(const byte* data, size_t len)
{
	if(_isHeaderRecved)
	{
		if( _postData )
		{
			size_t maxLen = contentLength() - _postData->tellp();
			if( len > maxLen ) len = maxLen;
			return _postData->write(data, len);
		}
		else if( _postFile )
		{
			//size_t maxLen = contentLength() - ftell(_postFile);
			size_t maxLen = contentLength() - static_cast<size_t>(_postFile->tell());
			if( len > maxLen ) len = maxLen;
			//return fwrite(data, 1, len, _postFile);
			return _postFile->write(data, len);
		}
		else
		{
			/*
			* 根据 Content-Length 的值确定是在内存中缓存还是使用文件系统缓存.
			*/
			if( contentLength() > POST_DATA_CACHE_SIZE)
			{
				_postFileName = _server->tmpFileName();
				_postFile = new WINFile;
				if(!_postFile->open(AtoT(_postFileName).c_str(), WINFile::rw, true))
				{
					assert(0);
					LOGGER_CFATAL(theLogger, _T("无法打开临时文件[%s],错误码[%d].\r\n"), AtoT(_postFileName).c_str(), errno);
					return 0;
				}
			}
			else
			{
				_postData = new memfile(1024, POST_DATA_CACHE_SIZE);
				assert(_postData);
			}
			return push(data, len);
		}
	}
	else
	{
		/*
		* 一个字符一个字符写入,直到连续两个换行.
		*/

		size_t i = 0;
		for(; i < len; ++i)
		{
			if( 1 != _header.write(&data[i], 1))
			{
				/* 超出 HTTP Request 头的长度限制了 */
				/* 凡是从客户端读取的数据都是不安全的数据,所以设置了一个最大值 */
				assert(0);
				return 0;
			}

			if(is_end(reinterpret_cast<const byte*>(_header.buffer()), _header.fsize()))
			{
				/*
				* 已经接收到一个完整的请求头
				*/
				_contentLength = atoi(field("Content-Length").c_str());
				_isHeaderRecved = true;
				++i; 

				if(_contentLength >= MAX_POST_DATA)
				{
					/* 检查 content-length 长度是否超出限制 */
					assert(0);
					return 0;
				}

				if(i < len)
				{
					/* 还有数据 */
					i += push(data + i, len - i);
				}

				break;
			}
		}

		return i;
	}
}


bool HTTPRequest::isValid()
{
	if(_isHeaderRecved)
	{
		if( _postData ) return _postData->tellp() == contentLength();
		else if(_postFile) return contentLength() == static_cast<size_t>(_postFile->tell());
		else return contentLength() == 0;
	}
	return false;
}

size_t HTTPRequest::headerSize()
{
	return _header.fsize();
}

size_t HTTPRequest::size()
{
	if( _postFile )
	{
		return headerSize() + static_cast<size_t>(_postFile->tell());
	}
	else if( _postData )
	{
		return headerSize() + _postData->tellp();
	}
	else
	{
		return headerSize();
	}
}

bool HTTPRequest::keepAlive()
{
	return field("Connection") == std::string("keep-alive");
}

size_t HTTPRequest::contentLength()
{
	return _contentLength;
}

HTTP_METHOD HTTPRequest::method()
{
	// 取出 HTTP 方法
	char szMethod[MAX_METHODSIZE] = {0};
	int nMethodIndex = 0;
	for(size_t i = 0; i < MAX_METHODSIZE && i < _header.fsize(); ++i)
	{
		if(reinterpret_cast<const char*>(_header.buffer())[i] != ' ')
		{
			szMethod[nMethodIndex++] = reinterpret_cast<const char*>(_header.buffer())[i];
		}
		else
		{
			break;
		}
	}

	// 返回
	if( strcmp(szMethod, "GET") == 0 ) return METHOD_GET;
	if( strcmp(szMethod, "PUT") == 0 ) return METHOD_PUT;
	if( strcmp(szMethod, "POST") == 0 ) return METHOD_POST;
	if( strcmp(szMethod, "HEAD") == 0 ) return METHOD_HEAD;
	if( strcmp(szMethod, "DELETE") == 0 ) return METHOD_DELETE; // 删除
	if( strcmp(szMethod, "TRACE") == 0 ) return METHOD_TRACE;
	if( strcmp(szMethod, "CONNECT") == 0 ) return METHOD_CONNECT;

	return METHOD_UNKNOWN;
}

// 返回客户端请求对象, 如果返回空字符串,说明客户端请求格式错误.
std::string HTTPRequest::uri(bool decode)
{
	std::string strObject("");
	const char* lpszRequest = reinterpret_cast<const char*>(_header.buffer());
	const char *pStart = NULL, *pEnd = NULL;

	// 第一行的第一个空格的下一个字符开始是请求的文件名开始.
	for(size_t i = 0; i < _header.fsize(); ++i)
	{
		if(lpszRequest[i] == ' ')
		{
			pStart = lpszRequest + i + 1; 
			break;
		}
		if(lpszRequest[i] == '\n') break;
	}
	if(pStart == NULL)
	{
		// 找不到开始位置
		assert(0);
		return strObject;
	}

	// 从第一行的末尾方向查找第一个空格,实例: GET / HTTP/1.1
	pEnd = strstr(lpszRequest, "\r\n"); 
	if(pEnd == NULL || pEnd < pStart) 
	{
		/* 找不到结尾位置 */
		assert(0);
		return strObject;
	}

	// 把结尾的空格移除
	while(pEnd >= pStart)
	{
		if(pEnd[0] == ' ')
		{
			pEnd--;
			break;
		}
		pEnd--;
	}

	if(pEnd == NULL || pEnd < pStart)
	{
		assert(0);
	}
	else
	{
		strObject.assign(pStart, pEnd - pStart + 1);
	}

	if(decode) return decode_url(strObject);
	else return strObject;
}

std::string HTTPRequest::field(const char* pszKey)
{
	return get_field(reinterpret_cast<const char*>(_header.buffer()), pszKey);
}

bool HTTPRequest::range(__int64 &lFrom, __int64 &lTo)
{
	__int64 nFrom = 0;
	__int64 nTo = -1; // -1 表示到最后一个字节.

	const char* lpszRequest = reinterpret_cast<const char*>(_header.buffer());
	const char* pRange = strstr(lpszRequest, "\r\nRange: bytes=");
	if(pRange)
	{
		/*
		The first 500 bytes (byte offsets 0-499, inclusive):
		bytes=0-499
		The second 500 bytes (byte offsets 500-999, inclusive):
		bytes=500-999
		The final 500 bytes (byte offsets 9500-9999, inclusive):
		bytes=-500
		bytes=9500-
		The first and last bytes only (bytes 0 and 9999):
		bytes=0-0,-1
		Several legal but not canonical specifications of the second 500 bytes (byte offsets 500-999, inclusive):
		bytes=500-600,601-999
		bytes=500-700,601-999
		*/

		pRange += strlen("\r\nRange: bytes=");
		const char *pMinus = strchr(pRange, '-');
		if(pMinus)
		{
			char szFrom[200], szTo[200];
			memset(szFrom, 0, 200);
			memset(szTo, 0, 200);
			memcpy(szFrom, pRange, pMinus - pRange);
			nFrom = _atoi64(szFrom);

			pMinus++;
			pRange = strstr(pMinus, "\r\n");
			if(pMinus + 1 == pRange)
			{
				nTo = -1;
			}
			else
			{
				memcpy(szTo, pMinus, pRange - pMinus);
				nTo = _atoi64(szTo);
				if(nTo <= 0) nTo = -1;
			}

			lFrom = nFrom;
			lTo = nTo;

			return true;
		}
		else
		{
		}
	}
	else
	{
	}
	return false;
}

size_t HTTPRequest::read(byte* buf, size_t len)
{
	if(_postData)
	{
		return _postData->read(buf, len);
	}
	else if(_postFile)
	{
		return _postFile->read(buf, len);
		//return fread(buf, 1, len, _postFile);
	}
	else
	{
		return 0;
	}
}

bool HTTPRequest::eof()
{
	if(_postData)
	{
		return _postData->eof();
	}
	else if(_postFile)
	{
		//return feof(_postFile) != 0;
		return _postFile->eof();
	}
	else
	{
		return true;
	}
}

bool HTTPRequest::reset()
{
	_connId = INVALID_CONNID;
	_clientSock = NULL;
	if(_sockBuf)
	{
		delete []_sockBuf;
		_sockBuf = NULL;
	}
	_sockBufLen = 0;
	if(_postFile)
	{
		_postFile->close();
		delete _postFile;
		_postFile = NULL;

		// 临时文件,close() 后自动删除,不需要调用 remove()
		// WINFile::remove(AtoT(_postFileName).c_str());
	}
	if(_postData)
	{
		delete _postData;
		_postData = NULL;
	}
	
	_isHeaderRecved = false;
	_header.trunc();
	_bytesRecv = 0;
	_startTime = 0;
	return true;
}

int HTTPRequest::run(conn_id_t connId, iocp_key_t clientSock, size_t timeout)
{
	/*
	* timeout: 第一个 recv 操作的超时时间,如果是 keep-alive 保持的连接,这个值可能会和新建连接的值不同
	*/
	assert(_sockBuf == NULL);
	if(_sockBuf)
	{
		return CT_INTERNAL_ERROR;
	}

	_clientSock = clientSock;
	_sockBuf = new byte[MAX_SOCKBUFF_SIZE];
	_sockBufLen = MAX_SOCKBUFF_SIZE;
	_connId = connId;

	/*
	* 开始接收请求头
	*/
	if(IOCP_PENDING == _network->recv(_clientSock, _sockBuf, _sockBufLen, timeout, IOCPCallback, this))
	{
		return CT_SUCESS;
	}
	else
	{
		reset();
		return CT_CLIENTCLOSED;
	}
}

bool HTTPRequest::stop(int ec)
{
	return true;
}

void HTTPRequest::deleteSocketBuf()
{
	assert(_sockBuf);
	delete []_sockBuf;
	_sockBuf = NULL;
	_sockBufLen = 0;
}

void HTTPRequest::close(int exitCode)
{
	/* 删除缓冲区,不再需要了 */
	deleteSocketBuf();
	_server->onRequest(this, exitCode);
}

void HTTPRequest::onRecv(int flags, size_t bytesTransfered)
{
	/* 接收失败 */
	if(bytesTransfered == 0)
	{
		if(flags & IOCP_READTIMEO)
		{
			close(CT_RECV_TIMEO);
		}
		else
		{
			close(CT_CLIENTCLOSED);
		}
		return;
	}

	/* 接收成功 */

	/* 收到第一段请求头的数据时,作为请求开始的时间记录下来*/
	if(0 == _startTime) _startTime = _hrt.now();

	_bytesRecv += bytesTransfered;
	_server->onRequestDataReceived(this, bytesTransfered);

	size_t bytesPushed = push(_sockBuf, bytesTransfered);
	if(isValid())
	{
		assert(bytesTransfered == bytesPushed);

		/* 把文件指针移到起始位置,准备读 */
		if( _postFile )
		{
			//fseek(_postFile, 0, SEEK_SET);
			_postFile->seek(0, WINFile::s_set);
		}

		/* HTTP Request 接收完毕 */
		close(CT_SUCESS);
	}
	else
	{
		if( bytesPushed != bytesTransfered )
		{
			/* 超出长度限制,需要给客户端返回 400 错误,所以用 CT_SUCESS 退出码 */
			close(CT_SUCESS);
		}
		else
		{
			int netIoRet = _network->recv(_clientSock, _sockBuf, MAX_SOCKBUFF_SIZE, _server->recvTimeout(), IOCPCallback, this);
			if(IOCP_PENDING != netIoRet)
			{
				close(netIoRet == IOCP_SESSIONTIMEO ? CT_SESSION_TIMEO : CT_CLIENTCLOSED);
			}
			else
			{
				/* 继续接收 */
			}
		}
	}
}

void HTTPRequest::IOCPCallback(iocp_key_t s, int flags, bool result, int transfered, byte* buf, size_t len, void* param)
{
	HTTPRequest* instPtr = reinterpret_cast<HTTPRequest*>(param);
	assert(flags & IOCP_RECV);
	instPtr->onRecv(flags, transfered);
}

std::string HTTPRequest::getHeader()
{
	if(!_isHeaderRecved) return std::string("");

	return std::string(reinterpret_cast<char*>(_header.buffer()), _header.fsize());
}
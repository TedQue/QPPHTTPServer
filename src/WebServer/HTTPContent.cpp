/* Copyright (C) 2011 阙荣文
 *
 * 这是一个开源免费软件,您可以自由的修改和发布.
 * 禁止用作商业用途.
 *
 * 联系原作者: querw@sina.com 
*/

#include "StdAfx.h"
#include "HTTPContent.h"
#include "HTTPDef.h"
#include <io.h>
#include <assert.h>

HTTPContent::HTTPContent()
	: _openType(OPEN_NONE), _contentType(""), _fileName(""), _from(0), _to(0)
{
	memset(&_fileInf, 0, sizeof(_fileInf));
}

HTTPContent::~HTTPContent()
{
	close();
}

bool HTTPContent::open(const std::string &fileName, __int64 from, __int64 to)
{
	if(OPEN_NONE != _openType) return false;
	std::string strFileName = fileName;

	_file.open(AtoT(strFileName).c_str(), WINFile::r);
	//_file = fopen(strFileName.c_str(), "rb");
	//if(NULL == _file)
	if(!_file.isopen())
	{
	}
	else
	{
		if( 0 != _stat32i64(strFileName.c_str(), &_fileInf))
		{
			assert(0);
		}

		__int64 fileSize = _fileInf.st_size;

		_openType = OPEN_FILE;
		_fileName = fileName;
			
		_from = from;
		_to = to;
			
		// lFrom 应该大于0且小于文件的长度
		if(_from > 0 && _from < fileSize)
		{
			//_fseeki64(_file, _from, SEEK_SET);
			_file.seek(_from, WINFile::s_set);
		}
		else
		{
			_from = 0;
		}
			
		// lTo 应该大于等于 lFrom且小于文件的长度.
		if(_to >= _from && _to <  fileSize)
		{
		}
		else
		{
			_to = fileSize - 1;
		}
	}

	return OPEN_NONE != _openType;
}

/*
* 读取目录列表输出为一个HTML文本流.
*/
bool HTTPContent::open(const std::string &urlStr, const std::string &filePath)
{
	if(OPEN_NONE != _openType) return false;
	assert(_memfile.fsize() == 0);

	char buffer[_MAX_PATH + 100] = {0};
	char sizeBuf[_MAX_PATH + 100] = {0};

	// 生成一个UTF8 HTML文本流,包含了文件列表.
	
	// 1. 输出HTML头,并指定UTF-8编码格式
	writeString("<html><head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\"/></head>");
	writeString("<body>");

	// 2. 输出路径
	//(1). 输出第一项 根目录
	writeString("<A href=\"/\">/</A>");

	//(2). 其它目录
	std::string::size_type st = 1;
	std::string::size_type stNext = 1;
	while( (stNext = urlStr.find('/', st)) != std::string::npos)
	{
		std::string strDirName =  urlStr.substr(st, stNext - st + 1);
		std::string strSubUrl = urlStr.substr(0, stNext + 1);

		writeString("&nbsp;|&nbsp;");

		writeString("<A href=\"");
		writeString(AtoUTF8(strSubUrl).c_str());
		writeString("\">");
		writeString(AtoUTF8(strDirName).c_str());
		writeString("</A>");
		
		// 下一个目录
		st = stNext + 1;
	}
	writeString("<br /><hr />");

	// 3. 列出当前目录下的所有文件
	std::string strFullName;
	strFullName = filePath;
	if(strFullName.back() != '\\') strFullName += '\\'; // 如果不是以'\\'结尾的文件路径,则补齐. 注意一个原则:URL以正斜杠分隔;文件名以反斜杠分隔
	strFullName += "*";

	std::string strFiles(""); // 普通文件写在这个字符串中.

	__finddata64_t fileinfo;
	intptr_t findRet = _findfirst64(strFullName.c_str(), &fileinfo);
	if( -1 != findRet )
	{
		do 
		{
			// 跳过 . 文件
			if( stricmp(fileinfo.name, ".") == 0 || 0 == stricmp(fileinfo.name, "..") )
			{
				continue;
			}

			// 跳过系统文件和隐藏文件
			if( fileinfo.attrib & _A_SYSTEM || fileinfo.attrib & _A_HIDDEN )
			{
				continue;
			}

			// 输出子目录或者
			if( fileinfo.attrib & _A_SUBDIR )
			{
				// 如果是子目录,直接写入

				// 最后修改时间
				_ctime64_s( buffer, _countof(buffer), &fileinfo.time_write );
				writeString(AtoUTF8(buffer).c_str());

				// 目录名需要转换为UTF8编码
				sprintf(buffer, "%s/", fileinfo.name);
				std::string fileurl = AtoUTF8(urlStr.c_str());
				std::string filename = AtoUTF8(buffer);

				writeString("&nbsp;&nbsp;");
				writeString("<A href=\"");
				writeString(fileurl.c_str());
				writeString(filename.c_str());
				writeString("\">");
				writeString(filename.c_str());
				writeString("</A>");

				// 写入目录标志
				writeString("&nbsp;&nbsp;[DIR]");

				// 换行
				writeString("<br />");
			}
			else
			{
				// 普通文件,写入到一个缓冲的字符串string变量内,循环外再合并.这样,所有的目录都在前面,文件在后面
				_ctime64_s( buffer, _countof(buffer), &fileinfo.time_write );
				strFiles += AtoUTF8(buffer);

				// 文件名转换为UTF8编码再写入
				std::string filename = AtoUTF8(fileinfo.name);
				std::string fileurl = AtoUTF8(urlStr.c_str());

				strFiles += "&nbsp;&nbsp;";
				strFiles += "<A href=\"";
				strFiles += fileurl;
				strFiles += filename;
				strFiles += "\">";
				strFiles += filename;
				strFiles += "</A>";

				// 文件大小
				// 注: 由于Windows下 wsprintf 不支持 %f 参数,所以只好用 sprintf 了
				double filesize = 0;
				if( fileinfo.size >= G_BYTES)
				{
					filesize = (fileinfo.size * 1.0) / G_BYTES;
					sprintf(sizeBuf, "%.2f&nbsp;GB", filesize);
				}
				else if( fileinfo.size >= M_BYTES ) // MB
				{
					filesize = (fileinfo.size * 1.0) / M_BYTES;
					sprintf(sizeBuf, "%.2f&nbsp;MB", filesize);
				}
				else if( fileinfo.size >= K_BYTES ) //KB
				{
					filesize = (fileinfo.size * 1.0) / K_BYTES;
					sprintf(sizeBuf, "%.2f&nbsp;KB", filesize);
				}
				else // Bytes
				{
					sprintf(sizeBuf, "%lld&nbsp;Bytes", fileinfo.size);
				}
			
				strFiles += "&nbsp;&nbsp;";
				strFiles += sizeBuf;

				// 换行
				strFiles += "<br />";
			}
		} while ( -1 != _findnext64(findRet, &fileinfo));
		
		_findclose(findRet);
	}

	// 把文件字符串写入到 Content 中.
	if(strFiles.size() > 0)
	{
		writeString(strFiles.c_str());
	}

	// 4. 输出结束标志.
	writeString("</body></html>");

	_openType = OPEN_DIR;
	return true;
}

bool HTTPContent::open(const char* buf, int len, int type)
{
	if(OPEN_NONE != _openType) return false;

	assert(_memfile.fsize() == 0);
	if( len == write(buf, len) )
	{
		_openType = type;
	}
	else
	{
		assert(0);
	}

	return OPEN_NONE != _openType;
}

void HTTPContent::close()
{
	//assert( OPEN_NONE != _openType );

	_contentType = "";
	_fileName = "";

	
	_file.close();
	_memfile.trunc();

	_openType = OPEN_NONE;
}

bool HTTPContent::isOpen()
{
	return  OPEN_NONE != _openType;
}

std::string HTTPContent::getContentTypeFromFileName(const char* pszFileName)
{
	std::string strType = "application/octet-stream";

	const char *pExt = strrchr(pszFileName, '.');
	if(pExt && strlen(pExt) < 19)
	{
		char szExt[20];
		strcpy(szExt, pExt + 1);

		if(stricmp(szExt, "jpg") == 0)
		{
			strType =  "image/jpeg";
		}
		else if(stricmp(szExt, "txt") == 0)
		{
			strType = "text/plain";
		}
		else if(stricmp(szExt, "htm") == 0)
		{
			strType = "text/html";
		}
		else if(stricmp(szExt, "html") == 0)
		{
			strType = "text/html";
		}
		else if(stricmp(szExt, "gif") == 0)
		{
			strType = "image/gif";
		}
		else if(stricmp(szExt, "png") == 0)
		{
			strType = "image/png";
		}
		else if(stricmp(szExt, "bmp") == 0)
		{
			strType = "image/x-xbitmap";
		}
		else
		{
		}
	}

	return strType;
}

size_t HTTPContent::writeString(const char* pszString)
{
	return write((void *)pszString, strlen(pszString));
}

size_t HTTPContent::write(const void* buf, size_t len)
{
	if(_file.isopen())
	{
		/* 目前没有用到可写的 HTTPContent */
		assert(0);
		return _file.write(buf, len);
	}
	else
	{
		return _memfile.write(buf, len);
	}
}

std::string HTTPContent::contentType()
{
	std::string strType("application/octet-stream");

	if(_openType == OPEN_FILE)
	{
		strType = getContentTypeFromFileName(_fileName.c_str());
	}
	else if(_openType == OPEN_TEXT)
	{
		strType = "text/plain";
	}
	else if(_openType == OPEN_HTML)
	{
		strType = "text/html";
	}
	else if(_openType == OPEN_DIR)
	{
		strType = "text/html";
	}
	else
	{
	}

	return strType;
}


__int64 HTTPContent::contentLength()
{
	__int64 nLength = 0;

	if(_openType == OPEN_FILE)
	{
		nLength = _to - _from + 1;
	}
	else
	{
		nLength = _memfile.fsize();
	}

	return nLength;
}

std::string HTTPContent::lastModified()
{
	__int64 ltime;

	if(_openType == OPEN_FILE)
	{
		ltime = _fileInf.st_mtime;
	}
	else
	{
		_time64( &ltime );
	}

	return format_http_date(&ltime);
}

std::string HTTPContent::contentRange()
{
	char szRanges[300] = {0};
	if(_openType == OPEN_FILE)
	{
		sprintf(szRanges, "bytes %lld-%lld/%lld", _from, _to, _fileInf.st_size);
	}
	else
	{
		
	}
	return szRanges;
}

bool HTTPContent::isFile()
{
	return _file.isopen();
}

bool HTTPContent::eof()
{
	if(_file.isopen())
	{
		//if(feof(_file))
		if(_file.eof())
		{
			return true;
		}
		else
		{
			return _file.tell() >= _to;
			//return _ftelli64(_file) >= _to;
		}
	}
	else
	{
		return _memfile.eof();
	}
}

size_t HTTPContent::read(void* buf, size_t len)
{
	assert(len);

	if(_file.isopen())
	{
		int nRet = 0;
		//__int64 lCurPos = _ftelli64(_file);  // ftell()返回的是当前指针的位置,指向第一个未读的字节
		__int64 lCurPos = _file.tell();
		__int64 lLeftSize = _to - lCurPos + 1; // 文件中剩余的字节数

		if(len > lLeftSize)
		{
			len = static_cast<size_t>(lLeftSize);// 此处是安全的
		}
		//return fread(buf, 1, len, _file); 
		return _file.read(buf, len);
	}
	else
	{
		return _memfile.read(buf, len);
	}
}

std::string HTTPContent::etag()
{
	std::string strETag("");
	if(OPEN_FILE == _openType)
	{
		char szLength[201] = {0};
		//_ltoa_s((_to - _from + 1), szLength, 200, 10);
		_i64toa(_fileInf.st_size, szLength, 10);

		// 如果是文件, 根据文件大小和修改日期创建. [ETag: ec5ee54c00000000:754998030] 修改时间的HEX值:文件长度
		// 确保同一个资源的 ETag 是同一个值.
		// 即使客户端请求的只是这个资源的一部分.
		// 断点续传客户端根据 ETag 的值确定下载的几个部分是不是同一个文件.
		strETag = to_hex((const unsigned char*)(&_fileInf.st_mtime), sizeof(_fileInf.st_mtime));
		strETag += ":";
		strETag += szLength;
	}
	else
	{
		char szLength[201] = {0};
		 _ltoa_s(_memfile.fsize(), szLength, 200, 10); // 内存数据没必要用 __int64

		// 如果是内存数据, 根据大小和取若干个字节的16进制字符创建.
		unsigned char szValue[ETAG_BYTE_SIZE + 1];

		for(int i = 0; i < ETAG_BYTE_SIZE; ++i)
		{
			int nUnit = _memfile.fsize() / ETAG_BYTE_SIZE;
			szValue[i] = reinterpret_cast<const char*>(_memfile.buffer())[nUnit * i];
		}

		strETag = to_hex(szValue, ETAG_BYTE_SIZE);
		strETag += ":";
		strETag += szLength;
	}

	return strETag;
}


/*
int HTTPContent::Seek(int nOffset);
*/
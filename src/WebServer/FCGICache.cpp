#include "StdAfx.h"
#include "FCGICache.h"
#include "assert.h"
#include "HTTPDef.h"

FCGICache::FCGICache(size_t bufSize, const std::string &tmpFileName)
	: _buf(NULL), _bufSize(bufSize), _rpos(0), _wpos(0), _file(NULL), _fileName(tmpFileName), _frpos(0), _fwpos(0), _size(0)
{
}


FCGICache::~FCGICache()
{
	if( _buf )
	{
		delete []_buf;
	}

	if( _file )
	{
		_file->close();
		/*
		* 临时文件 close() 后自动删除
		if(!WINFile::remove(AtoT(_fileName).c_str()))
		{
			TRACE("WINFile::remove() %s failed,%s.\r\n", _fileName.c_str(), get_last_error().c_str());
		}
		*/

		delete _file;
	}
	else
	{
		/*
		* 由于每获得一个临时文件名,系统就将创建一个空文件.
		* 没有使用 _file 只需把这个空文件删除即可.
		*/
		if(!WINFile::remove(AtoT(_fileName).c_str()))
		{
			TRACE("WINFile::remove() %s failed,%s.\r\n", _fileName.c_str(), get_last_error().c_str());
		}
	}
}


size_t FCGICache::write(const void *buf, size_t len)
{
	size_t wr = 0;

	/* 分配缓冲 */
	if( _buf == NULL && _bufSize > 0 )
	{
		_buf = new byte[_bufSize];
		assert(_buf);
		if( NULL == _buf ) return 0;
		_rpos = 0;
		_wpos = 0;
	}

	/* 如果内存缓冲区内还有空间,则先写入内存 */
	if( _bufSize - _wpos >= len )
	{
		wr = len;
	}
	else
	{
		wr = _bufSize - _wpos;
	}
	if( wr > 0 )
	{
		memcpy(_buf + _wpos, buf, wr);
		_wpos += wr;
	}
	
	/* 剩下的数据写入文件中 */
	if( wr < len )
	{
		if(_file == NULL)
		{
			_file = new WINFile;
			_file->open(AtoT(_fileName).c_str(), WINFile::rw, true);
			assert(_file->isopen());
			_frpos = 0;
			_fwpos = 0;

			if(!_file->isopen())
			{
				delete _file;
				_file = NULL;
				LOGGER_CFATAL(theLogger, _T("无法打开临时文件[%s],错误码[%d].\r\n"), AtoT(_fileName).c_str(), errno);
			}
		}

		if( _file )
		{
			//fseek(_file, _fwpos, SEEK_SET);
			//size_t fwr = fwrite(reinterpret_cast<const byte*>(buf) + wr, 1, len - wr, _file);
			_file->seek(_fwpos, WINFile::s_set);
			size_t fwr = _file->write(reinterpret_cast<const byte*>(buf) + wr, len - wr);
			_fwpos += fwr;
			wr += fwr;
		}
	}

	// 记录总长度
	_size += wr;
	return wr;
}

size_t FCGICache::read(void *buf, size_t len)
{
	if(buf == NULL || len == 0) return 0;
	size_t rd = 0;

	/* 先从缓冲区内读取 */
	if( _buf )
	{
		if( _wpos - _rpos >= len )
		{
			rd = len;
		}
		else
		{
			rd = _wpos - _rpos;
		}

		if( rd > 0 )
		{
			memcpy(buf, _buf + _rpos, rd);
			_rpos += rd;
		}
	}

	/* 剩下的数据从文件中读取 */
	if( rd < len )
	{
		if( _file )
		{
			// fseek(_file, _frpos, SEEK_SET);
			// size_t frd = fread(reinterpret_cast<byte*>(buf) + rd, 1, len - rd, _file);
			_file->seek(_frpos, WINFile::s_set);
			size_t frd = _file->read(reinterpret_cast<byte*>(buf) + rd, len - rd);
			_frpos += frd;
			rd += frd;
		}
	}

	/* 填充缓冲区 */
	fillBuf();

	return rd;
}

size_t FCGICache::fillBuf()
{
	size_t frd = 0;
	if( _buf )
	{
		if( _frpos >= _fwpos )
		{
			/* 临时文件中没有数据,不需要移动 */
		}
		else 
		{
			/* 把内存缓冲剩余的数据移动到缓冲区开始位置 */
			if( _rpos < _wpos )
			{
				memmove(_buf, _buf + _rpos, _wpos - _rpos);
				_wpos -= _rpos;
				_rpos = 0;
			}

			if( _file )
			{
				/* 从临时文件中读取数据到内存缓冲区中 */
				//fseek(_file, _frpos, SEEK_SET);
				//frd = fread(_buf + _wpos, 1, _bufSize - _wpos, _file);
				_file->seek(_frpos, WINFile::s_set);
				frd = _file->read(_buf + _wpos, _bufSize - _wpos);
				_frpos += frd;
				_wpos += frd;

				/* 文件中的数据被读取完毕后,清空文件 */
				if(_fwpos == _frpos && _file->trunc())
				{
					_frpos = 0;
					_fwpos = 0;
				}
			}
		}
	}

	return frd;
}

bool FCGICache::empty()
{
	return (_rpos == _wpos) && (_frpos == _fwpos);
}

size_t FCGICache::puts(const char *str)
{
	return write(str, strlen(str));
}

size_t FCGICache::size()
{
	return _size;
}
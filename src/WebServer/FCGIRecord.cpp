#include "StdAfx.h"
#include "FCGIRecord.h"

#define FCGI_HEADER_SIZE FCGI_HEADER_LEN

void write_number(unsigned char* dest, unsigned int number, int bytes)
{
	/*
	* 高位在前
	*/
	if( 1 == bytes )
	{
		assert( number <= 127);
		*dest = static_cast<unsigned char>(number & 0x7F);

		/* 单字节数值的最高位必须为0 */
	}
	else if( 2 == bytes )
	{
		assert( number <= 65535);
		*dest = static_cast<unsigned char>((number >> 8) & 0xFF);
		*(dest + 1) = static_cast<unsigned char>(number & 0xFF);

		/* 双字节编码的数值最高位没要求 */
	}
	else if( 4 == bytes )
	{
		*dest = static_cast<unsigned char>((number >> 24) & 0xFF);
		*(dest + 1) = static_cast<unsigned char>((number >> 16) & 0xFF);
		*(dest + 2) = static_cast<unsigned char>((number >> 8) & 0xFF);
		*(dest + 3) = static_cast<unsigned char>(number & 0xFF);

		/* 最高位设置为1表示是一个4字节数值 */
		*dest |= 0x80;
	}
	else
	{
		assert(0);
	}
}

unsigned int read_number14(const unsigned char* src, size_t *bytes)
{
	if( (*src >> 7) == 0 )
	{
		/* 单字节 0 ~ 127 */
		if(bytes) *bytes = 1;
		return *src;
	}
	else
	{
		/* 4字节 */
		// ((B3 & 0x7f) << 24) + (B2 << 16) + (B1 << 8) + B0];
		if(bytes) *bytes = 4;
		return ((*src & 0x7f) << 24) + (*(src + 1) << 16) + (*(src + 2) << 8) + *(src + 3);
	}
}

unsigned int read_number2(const unsigned char* src)
{
	/* 2字节 */
	// ((B3 & 0x7f) << 24) + (B2 << 16) + (B1 << 8) + B0];
	return (*src << 8) + (*(src + 1));
}

size_t FCGIRecord::toNumber2(const unsigned char* src)
{
	return read_number2(src);
}

size_t FCGIRecord::toNumber14(const unsigned char* src, size_t *bytes)
{
	return read_number14(src, bytes);
}

void FCGIRecord::toBytes(void* dest, size_t number, size_t bytes)
{
	write_number(reinterpret_cast<unsigned char*>(dest), number, bytes);
}

FCGIRecord::FCGIRecord()
	: _buffer(1024, 65535 + FCGI_HEADER_SIZE) /* 这是一个 Fast CGI record 的最大长度 */
{
}

FCGIRecord::~FCGIRecord()
{
}

void FCGIRecord::reset()
{
	_buffer.trunc();
}

bool FCGIRecord::setHeader(unsigned short requestId, int type)
{
	FCGI_Header header;
	memset(&header, 0, FCGI_HEADER_SIZE);

	header.version = FCGI_VERSION_1;
	header.type = static_cast<unsigned char>(type);
	write_number(&header.requestIdB1, requestId, 2);
	

	_buffer.trunc();
	_buffer.write(&header, FCGI_HEADER_SIZE);
	return true;
}

bool FCGIRecord::setBeginRequestBody(unsigned short role, bool keepConn)
{
	if(getType() != FCGI_BEGIN_REQUEST) return false;

	FCGI_BeginRequestBody body;
	memset(&body, 0, sizeof(FCGI_BeginRequestBody));

	write_number(&body.roleB1, role, 2);
	if(keepConn) body.flags |= FCGI_KEEP_CONN;

	_buffer.write(&body, sizeof(FCGI_BeginRequestBody));
	return true;
}

bool FCGIRecord::setEndRequestBody(unsigned int appStatus, unsigned char protocolStatus)
{
	if(getType() != FCGI_END_REQUEST) return false;

	FCGI_EndRequestBody body;
	memset(&body, 0, sizeof(FCGI_EndRequestBody));

	write_number(&body.appStatusB3, appStatus, 4);
	body.protocolStatus= protocolStatus;

	_buffer.write(&body, sizeof(FCGI_EndRequestBody));
	return true;
}

bool FCGIRecord::setUnknownTypeBody()
{
	if(getType() != FCGI_UNKNOWN_TYPE) return false;

	FCGI_UnknownTypeBody body;
	memset(&body, 0, sizeof(FCGI_UnknownTypeBody));

	body.type = static_cast<unsigned char>(getType());
	
	_buffer.write(&body, sizeof(FCGI_UnknownTypeBody));
	return true;
}

bool FCGIRecord::addNameValuePair(const char* nstr, const char* vstr)
{
	nv_t n, v;
	n.len = strlen(nstr);
	n.data = reinterpret_cast<const byte*>(nstr);

	v.len = strlen(vstr);
	v.data = reinterpret_cast<const byte*>(vstr);

	return addNameValuePair(n, v);
}

bool FCGIRecord::addNameValuePair(nv_t n, nv_t v)
{
	if(getType() != FCGI_PARAMS) return false;
	unsigned char number[4];
	if(n.len <= 127)
	{
		write_number(number, n.len, 1);
		_buffer.write(number, 1);
	}
	else
	{
		write_number(number, n.len, 4);
		_buffer.write(number, 4);
	}
	if(v.len <= 127)
	{
		write_number(number, v.len, 1);
		_buffer.write(number, 1);
	}
	else
	{
		write_number(number, v.len, 4);
		_buffer.write(number, 4);
	}
	
	if(n.data != NULL)
	{
		_buffer.write(n.data, n.len);
	}
	if(v.data != NULL)
	{
		_buffer.write(v.data, v.len);
	}

	return true;
}

bool FCGIRecord::addBodyData(unsigned char* buf, size_t len)
{
	return _buffer.write(buf, len) == len;
}

bool FCGIRecord::setEnd()
{
	if(_buffer.fsize() < FCGI_HEADER_SIZE) return false;

	/* 把contentlength 写到 FCGI_Header 相应的字段中 */
	size_t contentLength = _buffer.fsize() - FCGI_HEADER_SIZE;
	FCGI_Header* header = const_cast<FCGI_Header*>(reinterpret_cast<const FCGI_Header*>(_buffer.buffer()));
	write_number(&header->contentLengthB1, contentLength, 2);
	return true;
}

const void* FCGIRecord::buffer()
{
	return _buffer.buffer();
}

size_t FCGIRecord::size()
{
	return _buffer.fsize();
}

size_t FCGIRecord::read(void* buf, size_t len)
{
	if( _buffer.fsize() < FCGI_HEADER_SIZE )
	{
		return _buffer.read(buf, len);
	}
	else
	{
		/*
		* padding 数据忽略.
		*/
		const FCGI_Header* header = reinterpret_cast<const FCGI_Header*>(_buffer.buffer());
		size_t bytesMax = getContentLength(*header) + FCGI_HEADER_SIZE - _buffer.tellg();
		size_t readLen = len;
		if( readLen > bytesMax)
		{
			readLen = bytesMax;
		}

		return _buffer.read(buf, readLen);
	}
}

size_t FCGIRecord::writeHeader(const void *buf, size_t len)
{
	size_t bytesWritten = 0;
	if(_buffer.fsize() < FCGI_HEADER_SIZE)
	{
		size_t requiredHeaderSize = FCGI_HEADER_SIZE - _buffer.fsize();

		/* 继续读取 FCGI_Header */
		if( len > requiredHeaderSize )
		{
			/* 足够读取到 FCGI_Header */
			bytesWritten = _buffer.write(buf, requiredHeaderSize);
		}
		else
		{
			/* 数据长度不足 FCGI_Header,等待下一次继续 */
			bytesWritten = _buffer.write(buf, len);
		}
	}

	return bytesWritten;
}

size_t FCGIRecord::write(const void* buf, size_t len)
{
	size_t bytesWritten = 0;

	/* 先写入 FCGI_Header */
	bytesWritten = writeHeader(buf, len);
	if( bytesWritten >= len) return bytesWritten;
	
	/* 获取内容长度 */
	assert( _buffer.fsize() >= FCGI_HEADER_SIZE );
	const FCGI_Header* header = reinterpret_cast<const FCGI_Header*>(_buffer.buffer());
	size_t contentLength = getContentLength(*header);
	size_t paddingLength = header->paddingLength;
	if(contentLength == 0)
	{
	}
	else
	{
		/* 写入内容 */
		size_t bytesMax = contentLength + FCGI_HEADER_SIZE + paddingLength - _buffer.fsize();
		if(len - bytesWritten > bytesMax)
		{
			bytesWritten += _buffer.write(reinterpret_cast<const byte*>(buf) + bytesWritten, bytesMax);
		}
		else
		{
			bytesWritten += _buffer.write(reinterpret_cast<const byte*>(buf) + bytesWritten, len - bytesWritten);
		}
	}


	return bytesWritten;
}

bool FCGIRecord::getHeader(FCGI_Header &header)
{
	if( _buffer.fsize() >= FCGI_HEADER_SIZE )
	{
		const FCGI_Header* headerPtr = reinterpret_cast<const FCGI_Header*>(_buffer.buffer());
		memcpy(&header, headerPtr, FCGI_HEADER_SIZE);
		return true;
	}
	else
	{
		return false;
	}
}

size_t FCGIRecord::getContentLength(const FCGI_Header &header)
{
	return read_number2(&header.contentLengthB1);
}

bool FCGIRecord::getBeginRequestBody(unsigned short &role, bool &keepConn)
{
	if( getType() != FCGI_BEGIN_REQUEST ) return false;

	const void* bodyStart = reinterpret_cast<const char*>(_buffer.buffer());
	const FCGI_BeginRequestRecord *recordPtr = reinterpret_cast<const FCGI_BeginRequestRecord*>(bodyStart);
	role = read_number2(&recordPtr->body.roleB1);
	keepConn = (recordPtr->body.flags & FCGI_KEEP_CONN) != 0;

	return true;
}

bool FCGIRecord::getEndRequestBody(unsigned int &appStatus, unsigned char &protocolStatus)
{
	if( getType() != FCGI_END_REQUEST ) return false;

	size_t bytes = 0;
	const void* bodyStart = reinterpret_cast<const char*>(_buffer.buffer());
	const FCGI_EndRequestRecord *recordPtr = reinterpret_cast<const FCGI_EndRequestRecord*>(bodyStart);
	appStatus = read_number14(&recordPtr->body.appStatusB3, &bytes);
	protocolStatus = recordPtr->body.protocolStatus;

	return true;
}

bool FCGIRecord::check()
{
	FCGI_Header header;
	if(getHeader(header))
	{
		return getContentLength(header) == (_buffer.fsize() - FCGI_HEADER_SIZE - header.paddingLength);
	}
	return false;
}

unsigned char FCGIRecord::getType()
{
	FCGI_Header header;
	if(getHeader(header))
	{
		return header.type;
	}
	return 0;
}

size_t FCGIRecord::getNameValuePairCount()
{
	if( !check() ) return 0;

	size_t count = 0;
	size_t nameBytes = 0;
	size_t valueBytes = 0;
	size_t nameLen = 0;
	size_t valueLen = 0;
	size_t pos = FCGI_HEADER_SIZE;

	const void* bodyStart = reinterpret_cast<const char*>(_buffer.buffer()) + FCGI_HEADER_SIZE;
	const unsigned char* content = reinterpret_cast<const unsigned char*>(bodyStart);
	
	while( pos < _buffer.fsize())
	{
		nameLen = read_number14(content, &nameBytes);
		valueLen = read_number14(content + nameBytes, &valueBytes);

		pos += nameBytes + nameLen + valueBytes + valueLen;
		content += nameBytes + nameLen + valueBytes + valueLen;
		++count;
	}

	return count;
}

bool FCGIRecord::getNameValuePair(int index, nv_t &n, nv_t &v)
{
	if( !check() ) return false;

	size_t count = 0;
	size_t nameBytes = 0;
	size_t valueBytes = 0;
	size_t nameLen = 0;
	size_t valueLen = 0;
	size_t pos = FCGI_HEADER_SIZE;

	const void* bodyStart = reinterpret_cast<const char*>(_buffer.buffer()) + FCGI_HEADER_SIZE;
	const unsigned char* content = reinterpret_cast<const unsigned char*>(bodyStart);

	while( pos < _buffer.fsize())
	{
		nameLen = read_number14(content, &nameBytes);
		valueLen = read_number14(content + nameBytes, &valueBytes);

		pos += nameBytes + nameLen + valueBytes + valueLen;
		content += nameBytes + nameLen + valueBytes + valueLen;
		if(++count == index)
		{
			const unsigned char* nameData = content + nameBytes + valueBytes;
			const unsigned char* valueData = nameData + nameLen;

			if(n.data == NULL)
			{
				n.len = nameLen;
			}
			else if(n.len >= nameLen)
			{
				n.data = nameData;
				n.len = nameLen;
			}
			else
			{
				break;
			}

			if(v.data == NULL)
			{
				v.len = valueLen;
			}
			else if(v.len >= valueLen)
			{
				v.data = valueData;
				v.len = valueLen;
			}
			else
			{
				break;
			}
			return true;
		}
	}

	return false;
}

size_t FCGIRecord::getBodyLength()
{
	if( _buffer.fsize() > FCGI_HEADER_SIZE )
	{
		return _buffer.fsize() - FCGI_HEADER_SIZE;
	}
	else
	{
		return 0;
	}
}

const void* FCGIRecord::getBodyData()
{
	if( getBodyLength() > 0 )
	{
		return reinterpret_cast<const char*>(_buffer.buffer()) + FCGI_HEADER_SIZE;
	}
	else
	{
		return NULL;
	}
}

/* 
** ======================================================================= 
* FCGIRecordWriter 实现
*
*/
FCGIRecordWriter::FCGIRecordWriter(memfile &buf)
	: _buf(buf), _headerPos(0)
{
}

FCGIRecordWriter::~FCGIRecordWriter()
{
}

size_t FCGIRecordWriter::write(const void *buf, size_t len)
{
	return _buf.write(buf, len);
}

size_t FCGIRecordWriter::writeHeader(unsigned short requestId, int type)
{
	FCGI_Header header;
	memset(&header, 0, FCGI_HEADER_SIZE);

	header.version = FCGI_VERSION_1;
	header.type = static_cast<unsigned char>(type);
	write_number(&header.requestIdB1, requestId, 2);

	/* 记录 record header 的位置 */
	size_t bytesWritten = write(&header, FCGI_HEADER_SIZE);
	if(FCGI_HEADER_SIZE == bytesWritten)
	{
		_headerPos = _buf.tellp() - FCGI_HEADER_SIZE;
	}
	return bytesWritten;
}

size_t FCGIRecordWriter::writeBeginRequestBody(unsigned short role, bool keepConn)
{
	FCGI_BeginRequestBody body;
	memset(&body, 0, sizeof(FCGI_BeginRequestBody));

	write_number(&body.roleB1, role, 2);
	if(keepConn) body.flags |= FCGI_KEEP_CONN;

	return write(&body, sizeof(FCGI_BeginRequestBody));
}

size_t FCGIRecordWriter::writeEndRequestBody(unsigned int appStatus, unsigned char protocolStatus)
{
	FCGI_EndRequestBody body;
	memset(&body, 0, sizeof(FCGI_EndRequestBody));

	write_number(&body.appStatusB3, appStatus, 4);
	body.protocolStatus= protocolStatus;

	return write(&body, sizeof(FCGI_EndRequestBody));
}

size_t FCGIRecordWriter::writeUnknownTypeBody(unsigned char t)
{
	FCGI_UnknownTypeBody body;
	memset(&body, 0, sizeof(FCGI_UnknownTypeBody));

	body.type = t;

	return write(&body, sizeof(FCGI_UnknownTypeBody));
}

size_t FCGIRecordWriter::writeNameValuePair(const char* nstr, const char* vstr)
{
	size_t nlen = strlen(nstr);
	size_t vlen = strlen(vstr);
	unsigned char numName[4], numVal[4];

	if(nlen <= 127)
	{
		write_number(numName, nlen, 1);
	}
	else
	{
		write_number(numName, nlen, 4);
	}
	if(vlen <= 127)
	{
		write_number(numVal, vlen, 1);
	}
	else
	{
		write_number(numVal, vlen, 4);
	}

	size_t bytesWritten = write(numName, nlen <= 127 ? 1 : 4);
	bytesWritten += write(numVal, vlen <= 127 ? 1 : 4);
	bytesWritten += write(nstr, nlen);
	bytesWritten += write(vstr, vlen);
	return bytesWritten;
}

size_t FCGIRecordWriter::writeBodyData(const unsigned char* buf, size_t len)
{
	return write(buf, len);
}

size_t FCGIRecordWriter::writeEnd()
{
	/* 定位 FCGI_Header并写入长度 */
	FCGI_Header *headerPtr = reinterpret_cast<FCGI_Header*>(reinterpret_cast<char*>(_buf.buffer()) + _headerPos);
	size_t contentLength = _buf.tellp() - _headerPos - FCGI_HEADER_SIZE;
	write_number(&headerPtr->contentLengthB1, contentLength, 2);

	return contentLength;
}

/* 
** ======================================================================= 
* FCGIRecordReader 实现
*
*/
FCGIRecordReader::FCGIRecordReader(const void *buf, size_t len)
	: _buffer(buf), _len(len), _pos(0)
{
}

FCGIRecordReader::~FCGIRecordReader()
{
}

size_t FCGIRecordReader::putback(size_t len)
{
	_pos -= len;
	return len;
}

size_t FCGIRecordReader::space()
{
	return _len - _pos;
}

size_t FCGIRecordReader::read(void* dest, size_t len)
{
	if( space() < len ) return 0;
	if(dest) memcpy(dest, reinterpret_cast<const char*>(_buffer) + _pos, len);
	_pos += len;
	return len;
}

size_t FCGIRecordReader::readHeader(FCGI_Header &header)
{
	return read(&header, FCGI_HEADER_SIZE);
}

size_t FCGIRecordReader::readHeader(unsigned char &t, unsigned short &requestId, unsigned short &contentLength)
{
	FCGI_Header header;
	size_t rbytes = readHeader(header);
	if( rbytes == FCGI_HEADER_SIZE )
	{
		t = header.type;
		requestId = read_number2(&header.requestIdB1);
		contentLength = read_number2(&header.contentLengthB1);
	}
	return rbytes;
}

size_t FCGIRecordReader::readBeginRequestBody(unsigned short &role, bool &keepConn)
{
	FCGI_BeginRequestBody body;
	size_t rbytes = read(&body, sizeof(FCGI_BeginRequestBody));
	if( rbytes == sizeof(FCGI_BeginRequestBody))
	{
		role = read_number2(&body.roleB1);
		keepConn = body.flags & FCGI_KEEP_CONN;
	}

	return rbytes;
}

size_t FCGIRecordReader::readEndRequestBody(unsigned int &appStatus, unsigned char &protocolStatus)
{
	FCGI_EndRequestBody body;
	size_t rbytes = read(&body, sizeof(FCGI_EndRequestBody));

	if( rbytes == sizeof(FCGI_EndRequestBody))
	{
		appStatus = read_number14(&body.appStatusB3, NULL);
		protocolStatus = body.protocolStatus;
	}

	return rbytes;
}

size_t FCGIRecordReader::readBodyData(void* buf, size_t len)
{
	return read(buf, len);
}

size_t FCGIRecordReader::pos()
{
	return _pos;
}

size_t FCGIRecordReader::readNameValuePair(const char* &n, size_t &nameLen, const char* &val, size_t &valLen)
{
	size_t rbytes = 0;
	unsigned char number[4] = {0};
	size_t nlen = 0, vlen = 0;
	bool sucess = false;

	do
	{
		/* 读取name长度 */
		/* 读取第一个字节判断是否是1位编码还是4位编码 */
		if( read(number, 1) <= 0) break;
		++rbytes;
		if( number[0] & 0x80 )
		{
			/* 4位编码 */
			if( read(&number[1], 3) <= 0 ) break;
			rbytes += 3;
			nlen = read_number14(number, NULL);
		}
		else
		{
			/* 1位编码 */
			nlen = read_number14(number, NULL);
		}

		/* 读取value长度 */
		if( read(number, 1) <= 0) break;
		++rbytes;
		if( number[0] & 0x80 )
		{
			/* 4位编码 */
			if( read(&number[1], 3) <= 0 ) break;
			rbytes += 3;
			vlen = read_number14(number, NULL);
		}
		else
		{
			/* 1位编码 */
			vlen = read_number14(number, NULL);
		}

		/* 读取name */
		const char *tmpName = reinterpret_cast<const char*>(_buffer) + pos();
		if( read(NULL, nlen) != nlen ) break;
		rbytes += nlen;

		/* 读取value */
		const char *tmpVal = reinterpret_cast<const char*>(_buffer) + pos();
		if( read(NULL, vlen) != vlen ) break;
		rbytes += vlen;

		/* 读取成功 */
		sucess = true;
		nameLen = nlen;
		valLen = vlen;
		n = tmpName;
		val = tmpVal;

	}while(0);

	if(!sucess)
	{
		putback(rbytes);
		return 0;
	}
	else
	{
		return rbytes;
	}
}
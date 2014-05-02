#pragma once
/* Copyright (C) 2011 阙荣文
 *
 * 这是一个开源免费软件,您可以自由的修改和发布.
 * 禁止用作商业用途.
 *
 * 联系原作者: querw@sina.com 
*/

/*
* 互斥锁
*/

class Lock
{
private:
	CRITICAL_SECTION _cs;

public:
	Lock(void);
	~Lock(void);
	void lock();
	void unlock();
};

/*
* 读写锁
* 当锁被占用,有其他锁请求等待进入时,两种思路
* 1. 使用自动事件,一次只释放一个锁请求线程. 然后根据情况确定是否再次触发事件. 相当于串行序列.
* 1.1 优点: 效率更高,没有冗余操作.
* 1.2 缺点: 速度稍慢,要触发多次事件.

* 2. 使用手动事件,释放所有的锁请求线程,竞争锁.
* 2.1 优点: 结构更简单,速度稍快.
* 2.2 缺点: 有冗余操作,所有被释放的线程可能只有一个得到控制权,其他线程转一下之后要继续挂起.
*
* 如果应用场景中,读锁的数量远大于写锁的数量,那么用第二种方式能发挥最高效率.
*
* 另外,在unlock()中也有两种处理方式
* 1. 使用自动事件,触发事件后,不释放互斥量,这样被释放的那个线程就将占有互斥量.如果此时外部请求一个锁,那么它得不到竞争机会.
* 2. 使用自动事件,触发事件后,释放互斥量. 被释放的那个线程将重新竞争互斥量的所有权.
*/

class RWLock
{
private:
	int _st; /* 锁状态值 */
	int _rlockCount; /* 读锁计数 */
	int _rwaitingCount; /* 读等待计数 */
	int _wwaitingCount; /* 写等待计数 */
	HANDLE _ev; /* 通知事件 Event */
	//HANDLE _stLock; /* 访问状态值互斥量 */ /* 如果需要等待超时,则用 Mutex */
	CRITICAL_SECTION _stLock;

public:
	RWLock(void);
	~RWLock(void);
	void rlock();
	void wlock();
	void unlock();
};

/*
* 简单的同步锁池.
* 原则是有多少个CPU数量就只需要多少个同步锁,否则是多余的.
*/

template <typename lock_t>
class LockPool
{
private:
	size_t _size;
	lock_t** _lockList;
	size_t _index;

	LockPool(LockPool&);
	const LockPool& operator = (const LockPool&);

public:
	LockPool();
	~LockPool();

	bool init(size_t sz);
	bool destroy();

	lock_t* allocate();
	void recycle(lock_t* lockPtr);
};

template <typename lock_t>
LockPool<lock_t>::LockPool()
	: _lockList(NULL),
	_size(0),
	_index(0)
{
}

template <typename lock_t>
LockPool<lock_t>::~LockPool()
{
}

template <typename lock_t>
bool LockPool<lock_t>::init(size_t sz)
{
	_size = sz;
	if(_size == 0)
	{
		SYSTEM_INFO sysInfo;
		GetSystemInfo(&sysInfo);
		_size = sysInfo.dwNumberOfProcessors;
	}
	if(_size <= 0) sz = 2;

	_lockList = new lock_t*[_size];
	for(size_t i = 0; i < _size; ++i)
	{
		_lockList[i] = new lock_t;
	}

	_index = 0;
	return true;
}

template <typename lock_t>
bool LockPool<lock_t>::destroy()
{
	if(_lockList && _size > 0)
	{
		for(size_t i = 0; i < _size; ++i)
		{
			delete _lockList[i];
		}
		delete []_lockList;
	}
	_lockList = NULL;
	_size = 0;
	_index = 0;

	return true;
}

template <typename lock_t>
lock_t* LockPool<lock_t>::allocate()
{
	return _lockList[(_index++) % _size];
}

template <typename lock_t>
void LockPool<lock_t>::recycle(lock_t* lockPtr)
{

}
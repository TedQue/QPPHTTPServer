#include "StdAfx.h"
#include <assert.h>
#include "TimerQueue.h"

#define TIMER_ST_NORMAL 0
#define TIMER_ST_PROCESSING 1
#define TIMER_ST_EXPIRED 2

HighResolutionTimer::HighResolutionTimer(bool high)
{
	if(high)
	{
		/* 使用高精度计数器 */
		LARGE_INTEGER freq;
		if(!QueryPerformanceFrequency(&freq))
		{
			/* 硬件不支持,只能使用毫秒作为计数器 */
			_frequency = 0;
		}
		else
		{
			_frequency = freq.QuadPart;
		}
	}
	else
	{
		/* 使用毫秒作为计数器 */
		_frequency = 0;
	}
}

HighResolutionTimer::~HighResolutionTimer()
{
}

__int64 HighResolutionTimer::now()
{
	/* 返回当前时间对应的counter*/
	LARGE_INTEGER counter;
	if(_frequency == 0)
	{
		return GetTickCount();
	}
	else
	{
		if(!QueryPerformanceCounter(&counter)) return GetTickCount();
		return counter.QuadPart;
	}
}

__int64 HighResolutionTimer::getCounters(__int64 ms)
{
	/* 把ms时间换算为计数个数 */
	if( 0 == _frequency )
	{
		/* 硬件不支持高精度计数,毫秒数就是counter */
		return ms;
	}
	else
	{
		return ms * _frequency / 1000;
	}
}

__int64 HighResolutionTimer::getMs(__int64 counter)
{
	if( 0 == _frequency )
	{
		/* 硬件不支持高精度计数,此时 counter 就是 毫秒*/
		return counter;
	}
	else
	{
		return static_cast<__int64>((counter * 1.0 / _frequency) * 1000);
	}
}

/*
* TimerQueue 实现 **************************************************************
********************************************************************************
*/
#define OPP_ADD 1
#define OPP_CHANGE 2
#define OPP_REMOVE 3

#define WT_UNDIFINE 0
#define WT_TIMEOUT 1
#define WT_RESET 2


TimerQueue::TimerQueue(size_t poolSize /* = 256 */)
	: _waitableTimer(NULL),
	_timerThread(0),
	_hrt(true),
	_lock(NULL),
	_timerList(NULL),
	_poolSize(poolSize),
	_poolPos(0),
	_timerPool(NULL),
	_oppList(NULL),
	_wakeupType(WT_UNDIFINE),
	_curTimer(NULL),
	_stop(false)
{
	
}

TimerQueue::~TimerQueue()
{
	destroy();
}

int TimerQueue::init()
{
	assert(_waitableTimer == NULL);
	/*
	* 创建等待对象和事件对象
	*/
	if( NULL == (_waitableTimer = CreateWaitableTimer(NULL, FALSE, NULL)))
	{
		assert(0);
		return TQ_ERROR;
	}

	/*
	* 分配定时器队列和操作队列
	*/
	_lock = new Lock;
	_timerList = new timer_list_t;
	if(_poolSize > 0)
	{	
		_timerPool = new timer_desc_t*[_poolSize];
	}
	_oppList = new opp_list_t;

	/*
	* 创建工作线程
	*/
	if( 0 == (_timerThread = _beginthreadex(NULL, 0, workerProc, this, 0, NULL)))
	{
		assert(0);
		return TQ_ERROR;
	}

	_stop = false;
	return TQ_SUCCESS;
}

int TimerQueue::destroy()
{
	/*
	* 停止工作线程,然后回收资源.
	*/
	if(_timerThread)
	{
		assert(_oppList);

		/*
		* 添加一个空的定时器作为停止的标志.
		* 然后等待工作线程退出
		*/
		_lock->lock();
		_stop = true;
		wakeup();
		_lock->unlock();

		WaitForSingleObject(reinterpret_cast<HANDLE>(_timerThread), INFINITE);
		CloseHandle(reinterpret_cast<HANDLE>(_timerThread));
		_timerThread = 0;
	}

	if(_waitableTimer)
	{
		CloseHandle(_waitableTimer);
		_waitableTimer = NULL;
	}

	/*
	* 删除所有定时器
	*/
	if(_timerList)
	{
		for(timer_list_t::iterator iter = _timerList->begin(); iter != _timerList->end(); ++iter)
		{
			freeTimer(*iter);
		}
		if(_timerList->size() > 0)
		{
			TRACE(_T("Undeleted timer:%d.\r\n"), _timerList->size());
			_timerList->clear();
		}
		delete _timerList;
		_timerList = NULL;
	}

	if(_timerPool)
	{
		for(size_t i = 0; i < _poolPos; ++i)
		{
			// 不能调用 freeTimer(),应该直接删除.
			delete _timerPool[i];
		}
		delete []_timerPool;
		_timerPool = NULL;
		_poolPos = 0;
	}

	if(_oppList)
	{
		delete _oppList;
		_oppList = NULL;
	}

	if(_lock)
	{
		delete _lock;
		_lock = NULL;
	}

	_stop = false;
	_curTimer = NULL;
	_wakeupType = WT_UNDIFINE;
	return TQ_SUCCESS;
}

TimerQueue::timer_desc_t* TimerQueue::allocTimer()
{
	timer_desc_t* timerPtr = NULL;
	if(_timerPool && _poolPos > 0)
	{
		timerPtr = _timerPool[--_poolPos];
	}
	else
	{
		timerPtr = new timer_desc_t;
	}
	memset(timerPtr, 0, sizeof(timer_desc_t));
	return timerPtr;
}

int TimerQueue::freeTimer(timer_desc_t* timerPtr)
{
	if(_timerPool && _poolPos < _poolSize)
	{
		_timerPool[_poolPos++] = timerPtr;
	}
	else
	{
		delete timerPtr;
	}
	return 0;
}

bool TimerQueue::isValidTimer(timer_desc_t* timerPtr)
{
	return timerPtr->st == TIMER_ST_NORMAL;
}

TimerQueue::timer_desc_t* TimerQueue::getFirstTimer()
{
	for(timer_list_t::iterator iter = _timerList->begin(); iter != _timerList->end(); ++iter)
	{
		if(isValidTimer(*iter))
		{
			return *iter;
		}
	}
	return NULL;
}

bool TimerQueue::isFirstTimer(timer_desc_t* timerPtr)
{
	return getFirstTimer() == timerPtr;
}

int TimerQueue::setNextTimer()
{
	/*
	* 重新设置定时器的时间
	*/
	_wakeupType = WT_TIMEOUT;
	_curTimer = getFirstTimer();

	if(NULL == _curTimer)
	{
		/* 没有定时器需要设置 */
		if(!CancelWaitableTimer(_waitableTimer))
		{
			assert(0);
		}
	}
	else
	{
		LARGE_INTEGER dueTime;
		__int64 curCounters = _hrt.now();
		if(curCounters >= _curTimer->expireCounters)
		{
			/*
			* 定时器已经超时
			*/
			dueTime.QuadPart = 0;
		}
		else
		{
			/*
			* 定时器还没有超时
			*/
			__int64 ms;
			ms = _hrt.getMs(_curTimer->expireCounters - curCounters);
			
			dueTime.QuadPart = ms * 1000 * 10;
			dueTime.QuadPart = ~dueTime.QuadPart + 1; /*负数在计算机内表示为绝对值取反再加一(取反时符号位已经变为1了)*/

			//LOGGER_CINFO(theLogger, _T("Set timer expire after %dms.\r\n"), static_cast<int>(ms));
		}
		
		if(!SetWaitableTimer(_waitableTimer, &dueTime, 0, NULL, NULL, FALSE))
		{
			LOGGER_CFATAL(theLogger, _T("SetWaitableTimer failed, error code:%d, handle:%x.\r\n"), GetLastError(), _waitableTimer );
			assert(0);
		}
		//TRACE(_T("SetTimer at:%d.\r\n"), curTime);
	}

	return 0;
}

void TimerQueue::oppExecute()
{
	if(!_oppList->empty())
	{
		for(opp_list_t::iterator iter = _oppList->begin(); iter != _oppList->end(); ++iter)
		{
			if(OPP_ADD == iter->second)
			{
				inqueue(iter->first);
			}
			else if(OPP_CHANGE == iter->second)
			{
				_timerList->remove(iter->first);
				inqueue(iter->first);
			}
			else if(OPP_REMOVE == iter->second)
			{
				_timerList->remove(iter->first);
				freeTimer(iter->first);
			}
			else
			{
				assert(0);
			}
		}
		_oppList->clear();
	}
}

bool TimerQueue::proc(HANDLE ce)
{
	_lock->lock();

	/* 是否退出 */
	if(_stop)
	{
		_lock->unlock();
		return false;
	}

	/*
	* 如果是手动设置的唤醒,则先处理操作队列,因为此时队头定时将要被修改
	*/
	if(WT_RESET == _wakeupType)
	{
		oppExecute();
	}
	else
	{
		/*
		* 处理当前定时器.
		*/
		assert(_curTimer);
		timer_desc_t* timerDescPtr = _curTimer;
		timerDescPtr->st = TIMER_ST_PROCESSING;
		timerDescPtr->ev = ce;
		_lock->unlock();

		/*
		* 调试查看定时器精度.
		*/
		//int ms = static_cast<int>(_hrt.getMs(_hrt.now() - timerDescPtr->expireCounters));
		//LOGGER_CINFO(theLogger, _T("TimerQueue - Timer deviation: %dms\r\n"), ms);
		//TRACE(_T("TimerQueue - Timer deviation: %dms\r\n"), ms);

		/* 执行回调函数 */
		if(timerDescPtr->funcPtr) timerDescPtr->funcPtr(timerDescPtr->param, TRUE);
	
		/*
		* 由于前面设置了 TIMER_ST_PROCESSING 标志,所以可以确保 timerDescPtr 指针此时是有效的.
		* 设定结束标志,发送完成通知
		*/
		_lock->lock();
		timerDescPtr->st = TIMER_ST_EXPIRED;
		if(timerDescPtr->waitting)
		{
			SetEvent(timerDescPtr->ev);
		}
		if(timerDescPtr->autoDelete)
		{
			_timerList->remove(timerDescPtr);
			freeTimer(timerDescPtr);
		}

		/* 当前定时器已经处理完毕,执行操作队列 */
		oppExecute();
	}

	/* 设置下一个定时器 */
	setNextTimer();

	_lock->unlock();
	return  true;
}

unsigned int __stdcall TimerQueue::workerProc(void* param)
{
	TimerQueue* instPtr = reinterpret_cast<TimerQueue*>(param);
	HANDLE completeEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(!completeEvent)
	{
		LOGGER_CFATAL(theLogger, _T("无法创建事件对象,错误码:%d\r\n"), GetLastError());
		return 1;
	}

	int ret = 0;
	while(true)
	{
		if( WAIT_OBJECT_0 == WaitForSingleObject(instPtr->_waitableTimer, INFINITE) )
		{
			if(!instPtr->proc(completeEvent))
			{
				/* 正常退出标志 */
				break;
			}
		}
		else
		{
			/*
			* 其他错误 WAIT_FAILED
			*/
			ret = 2;
			break;
		}
	}

	/*
	* 退出
	*/
	CloseHandle(completeEvent);
	return ret;
}

bool TimerQueue::inqueue(timer_desc_t* newTimerPtr)
{
	/*
	* _timerList 是一个有序队列(对于所有有效的定时器而言)
	* 对于大多数定时器队列,后添加的定时器一般也更晚超时,所以从末尾往前找往往效率更高一些.
	* 如果不是,那也没什么坏处.
	*/
	bool inserted = false;
	if(!_timerList->empty())
	{
		timer_list_t::iterator iter = _timerList->end();
		do
		{
			--iter;
			timer_desc_t* timerPtr = *iter;
			if(isValidTimer(timerPtr) && newTimerPtr->expireCounters >= timerPtr->expireCounters)
			{
				// std::list insert 在指定迭代器的**前面**插入新元素,这里需要的是新定时器应该在
				// 后面,所以先把迭代器往后移动
				++iter;
				_timerList->insert(iter, newTimerPtr);
				inserted = true;
				break;
			}
		}while(iter != _timerList->begin());
	}

	if(!inserted)
	{
		/* 没有找到合适的位置则添加到队头 */
		_timerList->push_front(newTimerPtr);
		return true;
	}
	else
	{
		/* insert 总是在 iter 之后插入,所以新定时器肯定不在队头 */
		return false;
	}
}

TimerQueue::timer_t TimerQueue::createTimer(DWORD timeout, timer_func_t callbackFunc, void* param)
{
	
	/*
	* 把新创建的定时器按照超时的时间插入到合适的位置,要确保第一个定时器肯定是最先超时的定时器.
	*/
	_lock->lock();
	timer_desc_t* newTimerPtr = allocTimer();
	newTimerPtr->expireCounters = _hrt.now() + _hrt.getCounters(timeout);
	newTimerPtr->st = TIMER_ST_NORMAL;
	newTimerPtr->funcPtr = callbackFunc;
	newTimerPtr->param = param;
	newTimerPtr->waitting = false;

	oppAcquire(newTimerPtr, OPP_ADD);
	_lock->unlock();

	return newTimerPtr;
}

/*
* 修改定时器,即使定时器已经过期,仍然有效.
*/
int TimerQueue::changeTimer(timer_t timerPtr, DWORD timeout)
{
	timer_desc_t* timerDescPtr = reinterpret_cast<timer_desc_t*>(timerPtr);
	_lock->lock();
	timerDescPtr->expireCounters = _hrt.now() + _hrt.getCounters(timeout);
	timerDescPtr->st = TIMER_ST_NORMAL;
	timerDescPtr->waitting = false;

	oppAcquire(timerDescPtr, OPP_CHANGE);
	_lock->unlock();
	return TQ_SUCCESS;
}

int TimerQueue::deleteTimer(timer_t timerPtr, bool wait)
{
	int ret = TQ_SUCCESS;
	timer_desc_t* timerDescPtr = reinterpret_cast<timer_desc_t*>(timerPtr);
	HANDLE ev = NULL;

	_lock->lock();
	if(timerDescPtr->st != TIMER_ST_PROCESSING)
	{
		/* 定时器空闲 */
		oppAcquire(timerDescPtr, OPP_REMOVE);
	}
	else
	{
		/* 工作线程正在执行回调函数 */
		timerDescPtr->waitting = wait;
		timerDescPtr->autoDelete = true;

		/* 正在执行定时器回调函数 */
		if(wait)
		{
			/* 等待回调函数返回 */
			ev = timerDescPtr->ev;
		}
	}
	_lock->unlock();

	/* 等待回调函数执行完毕(因为前面设置了自动删除标志,工作线程在回调函数执行完毕之后会把定时器删除) */
	if(ev != NULL)
	{
		WaitForSingleObject(ev, INFINITE);
	}
	return ret;
}

void TimerQueue::oppAcquire(timer_desc_t* timerPtr, int oppType)
{
	_oppList->push_back(std::make_pair(timerPtr, oppType));
	
	/*
	* 只有队头被移动后才需要唤醒工作线程,重新设置定时器
	*/
	bool wake = false;
	if(oppType == OPP_ADD)
	{
		// 新定时器比当前定时器更快超时,需要重新设置定时器.
		wake = (_curTimer == NULL || timerPtr->expireCounters < _curTimer->expireCounters);
	}
	else if(oppType == OPP_CHANGE)
	{
		// 当前定时器被更改或者修改后的定时器比当前定时器更快超时.
		wake = (timerPtr == _curTimer || timerPtr->expireCounters < _curTimer->expireCounters);
	}
	else
	{
		// 当前定时器被删除.
		wake = (_curTimer == NULL || timerPtr == _curTimer);
	}

	if(wake)
	{
		wakeup();
	}
}

void TimerQueue::wakeup()
{
	if(_wakeupType != WT_RESET)
	{
		_wakeupType = WT_RESET;

		LARGE_INTEGER dueTime;
		dueTime.QuadPart = 0;

		if(!SetWaitableTimer(_waitableTimer, &dueTime, 0, NULL, NULL, FALSE))
		{
			LOGGER_CFATAL(theLogger, _T("wakeup failed, error code:%d, handle:%x.\r\n"), GetLastError(), _waitableTimer );
			assert(0);
		}
	}
	else
	{
		/*
		* 已经手动触发 _waitableTimer, 不需要再次触发
		*/
	}
}
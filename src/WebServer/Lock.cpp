#include "StdAfx.h"
#include <assert.h>
#include "Lock.h"

/*
* 读写锁状态
*/
#define RWLOCK_IDLE 0 /* 空闲 */
#define RWLOCK_R 0x01 /* 读锁 */
#define RWLOCK_W 0x02 /* 写锁 */
//#define RWLOCK_IS_WAITING 0x04 /* 是否有锁请求正在等待 */
//#define USE_SYNCLOCK

Lock::Lock(void)
{
	InitializeCriticalSection(&_cs);
}


Lock::~Lock(void)
{
	DeleteCriticalSection(&_cs);
}

void Lock::lock()
{
	EnterCriticalSection(&_cs);
}

void Lock::unlock()
{
	LeaveCriticalSection(&_cs);
}

RWLock::RWLock(void)
	: _rlockCount(0),
	_st(RWLOCK_IDLE),
	_rwaitingCount(0),
	_wwaitingCount(0)
{
	//_stLock = CreateMutex(NULL, FALSE, NULL);
	//assert(_stLock != INVALID_HANDLE_VALUE);
	InitializeCriticalSection(&_stLock);

	/*
	* 假设当前有多个读锁请求正在等待写锁释放,那么当写锁被释放时,所有这些读锁都应该有机会获得执行.
	*/
	_ev = CreateEvent(NULL, TRUE, FALSE, NULL);
	assert(_ev != INVALID_HANDLE_VALUE);
}


RWLock::~RWLock(void)
{
	//CloseHandle(_stLock);
	DeleteCriticalSection(&_stLock);
	CloseHandle(_ev);
}

#ifdef USE_SYNCLOCK
void RWLock::rlock()
{
	EnterCriticalSection(&_stLock);
}

void RWLock::wlock()
{
	EnterCriticalSection(&_stLock);
}

void RWLock::unlock()
{
	LeaveCriticalSection(&_stLock);
}
#else
void RWLock::rlock()
{
	bool isWaitReturn = false;
	while(1)
	{
		//WaitForSingleObject(_stLock, INFINITE);
		EnterCriticalSection(&_stLock);
		if(isWaitReturn)
		{
			/*
			* 等待事件返回,重新竞争锁.
			*/
			--_rwaitingCount;
		}

		if(_st == RWLOCK_IDLE)
		{
			/*
			* 空闲状态,直接得到控制权
			*/
			_st = RWLOCK_R;
			_rlockCount++;
			//ReleaseMutex(_stLock);
			LeaveCriticalSection(&_stLock);
			break;
		}
		else if( _st == RWLOCK_R)
		{
			if(_wwaitingCount > 0)
			{
				/*
				* 有写锁正在等待,则一起等待,以使写锁能获得竞争机会.
				*/
				++_rwaitingCount;
				ResetEvent(_ev);
				//SignalObjectAndWait(_stLock, _ev, INFINITE, FALSE);
				LeaveCriticalSection(&_stLock);

				/*
				* 虽然 LeaveCriticalSection() 和 WaitForSingleObject() 之间有一个时间窗口,
				* 但是由于windows平台的事件信号是不会丢失的,所以没有问题.
				*/
				WaitForSingleObject(_ev, INFINITE);

				/*
				* 等待返回,继续尝试加锁.
				*/
				isWaitReturn = true;
			}
			else
			{	
				/*
				* 得到读锁,计数+1
				*/
				++_rlockCount;
				//ReleaseMutex(_stLock);
				LeaveCriticalSection(&_stLock);
				break;
			}
		}
		else if(_st == RWLOCK_W)
		{
			/*
			* 等待写锁释放
			*/
			++_rwaitingCount;
			ResetEvent(_ev);
			//SignalObjectAndWait(_stLock, _ev, INFINITE, FALSE);
			LeaveCriticalSection(&_stLock);
			WaitForSingleObject(_ev, INFINITE);

			/*
			* 等待返回,继续尝试加锁.
			*/
			isWaitReturn = true;
		}
		else
		{
			assert(0);
			break;
		}
	}
}

void RWLock::wlock()
{
	bool isWaitReturn = false;

	while(1)
	{
		//WaitForSingleObject(_stLock, INFINITE);
		EnterCriticalSection(&_stLock);

		if(isWaitReturn) --_wwaitingCount;

		if(_st == RWLOCK_IDLE)
		{
			_st = RWLOCK_W;
			//ReleaseMutex(_stLock);
			LeaveCriticalSection(&_stLock);
			break;
		}
		else
		{
			++_wwaitingCount;
			ResetEvent(_ev);
			//SignalObjectAndWait(_stLock, _ev, INFINITE, FALSE);
			LeaveCriticalSection(&_stLock);
			WaitForSingleObject(_ev, INFINITE);

			isWaitReturn = true;
		}
	}
}

void RWLock::unlock()
{
	//WaitForSingleObject(_stLock, INFINITE);
	EnterCriticalSection(&_stLock);
	if(_rlockCount > 0)
	{
		/* 读锁解锁 */
		--_rlockCount;

		if( 0 == _rlockCount)
		{
			_st = RWLOCK_IDLE;

			/* 释放 */
			if( _wwaitingCount > 0 || _rwaitingCount > 0 )
			{
				/* 
				* 此时有锁请求正在等待,激活所有等待的线程.(手动事件).
				* 使这些请求重新竞争锁.
				*/
				SetEvent(_ev);
			}
			else
			{
				/* 空闲 */
			}
		}
		else
		{
			/* 还有读锁 */
		}
	}
	else
	{
		_st = RWLOCK_IDLE;

		/* 写锁解锁 */
		if( _wwaitingCount > 0 || _rwaitingCount > 0 )
		{
			/* 
			* 如果在占有互斥量_stLock的情况下,触发事件,那么可能会使一些锁请求不能得到竞争机会.
			* 假设调用unlock时,另一个线程正好调用rlock或者wlock.如果不释放互斥量,只有之前已经等待的锁请求有机会获得锁控制权.
			*/
			SetEvent(_ev);
		}
		else
		{
			/* 空闲 */
		}
	}
	//ReleaseMutex(_stLock);
	LeaveCriticalSection(&_stLock);
}
#endif
/*
* 另一个实现,网上找的.
* 完全竞争的读写锁, class RWLock 是写优先的读写锁.
*/
/****************************************************
 *  @file    $URL$
 *
 *  read-write lock c file
 *
 *  $Id$
 *
 *  @author gang chen <eyesfour@gmail.com>
 ***************************************************/

//
//#include <stdio.h>
//#include <stdlib.h>
//#define   WIN32_LEAN_AND_MEAN   
//#define _WIN32_WINNT 0x0400
//#include <Windows.h>
//#include "../../include/cg_rwlock.h"
//
//
//TRWLock  *RWLockCreate(void)
//{
//	TRWLock *hRWLock = (TRWLock*)malloc(sizeof(TRWLock));
//	if (hRWLock == NULL) return NULL;
//	// 创建用于保护内部数据的互斥量
//	hRWLock->hMutex = CreateMutex(NULL, FALSE, NULL);
//    // 创建用于同步共享访问线程的事件（手动事件）
//	hRWLock->hReadEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
//    // 创建用于同步独占访问线程的事件（自动事件）
//	hRWLock->hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
//	hRWLock->nSharedNum = 0;
//	hRWLock->nExclusiveNum = 0;
//	hRWLock->nLockType = LOCK_NONE;
//	return hRWLock;
//}
//
//void RWLockDestroy(TRWLock *hRWLock)
//{
//	CloseHandle(hRWLock->hMutex);
//	CloseHandle(hRWLock->hReadEvent);
//	CloseHandle(hRWLock->hWriteEvent);
//	free(hRWLock);
//}
//
//bool EnterReadLock(TRWLock *hRWLock, DWORD waitTime)
//{
//	WaitForSingleObject(hRWLock->hMutex, INFINITE);
//	++hRWLock->nSharedNum;
//	if (hRWLock->nLockType == LOCK_EXCLUSIVE) 
//	{
//		DWORD retCode = SignalObjectAndWait(hRWLock->hMutex, hRWLock->hReadEvent, /*waitTime*/INFINITE, FALSE);
//        if (retCode == WAIT_OBJECT_0) {
//            return true;
//        } else {
//            if (retCode == WAIT_TIMEOUT)
//                SetLastError(WAIT_TIMEOUT);
//            return false;
//        }
//    }
//	hRWLock->nLockType = LOCK_SHARED;
//	ReleaseMutex(hRWLock->hMutex);
//	return true;
//}
//
//
//void LeaveReadLock(TRWLock *hRWLock)
//{
//	//assert(hRWLock->nLockType == LOCK_SHARED);
//    WaitForSingleObject(hRWLock->hMutex, INFINITE);
//    --hRWLock->nSharedNum;
//    if (hRWLock->nSharedNum == 0) 
//	{
//		if (hRWLock->nExclusiveNum > 0) 
//		{
//            // 唤醒一个独占访问线程
//            hRWLock->nLockType = LOCK_EXCLUSIVE;
//			SetEvent(hRWLock->hWriteEvent);
//        } 
//		else 
//		{
//            // 没有等待线程
//            hRWLock->nLockType = LOCK_NONE;
//        }
//    }
//    ReleaseMutex(hRWLock->hMutex);
//}
//
//
//bool EnterWriteLock(TRWLock *hRWLock)
//{
//	WaitForSingleObject(hRWLock->hMutex, INFINITE);
//    ++hRWLock->nExclusiveNum;
//    if (hRWLock->nLockType != LOCK_NONE) 
//	{
//		DWORD retCode = SignalObjectAndWait(hRWLock->hMutex, hRWLock->hWriteEvent, /*waitTime*/INFINITE, FALSE);
//        if (retCode == WAIT_OBJECT_0) 
//		{
//            return true;
//        } 
//		else 
//		{
//            if (retCode == WAIT_TIMEOUT)
//                SetLastError(WAIT_TIMEOUT);
//            return false;
//        }
//    }
//    hRWLock->nLockType = LOCK_EXCLUSIVE;
//    ReleaseMutex(hRWLock->hMutex);
//    return true;
//}
//
//void LeaveWriteLock(TRWLock *hRWLock)
//{
//    //assert(hRWLock->nLockType == LOCK_EXCLUSIVE);
//    WaitForSingleObject(hRWLock->hMutex, INFINITE);
//    --hRWLock->nExclusiveNum;
//    // 独占访问线程优先
//    if (hRWLock->nExclusiveNum > 0) 
//	{
//        // 唤醒一个独占访问线程
//        SetEvent(hRWLock->hWriteEvent);
//    } 
//	else if (hRWLock->nSharedNum > 0) 
//	{
//        // 唤醒当前所有共享访问线程
//        hRWLock->nLockType = LOCK_SHARED;
//        PulseEvent(hRWLock->hReadEvent);
//    } 
//	else 
//	{
//        // 没有等待线程
//        hRWLock->nLockType = LOCK_NONE;
//    }
//    ReleaseMutex(hRWLock->hMutex);
//} ...
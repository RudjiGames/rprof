/*
 * Copyright (c) 2020 by Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#ifndef RPROF_MUTEX_H
#define RPROF_MUTEX_H

#include "rprof_platform.h"

namespace rprof {

#if RPROF_PLATFORM_WINDOWS || RPROF_PLATFORM_XBOXONE
	typedef CRITICAL_SECTION rprof_mutex;

	static inline void rprof_mutex_init(rprof_mutex* _mutex) {
		InitializeCriticalSection(_mutex);
	}

	static inline void rprof_mutex_destroy(rprof_mutex* _mutex) {
		DeleteCriticalSection(_mutex);
	}

	static inline void rprof_mutex_lock(rprof_mutex* _mutex) {
		EnterCriticalSection(_mutex);
	}

	static inline int rprof_mutex_trylock(rprof_mutex* _mutex)	{
		return TryEnterCriticalSection(_mutex) ? 0 : 1;
	}

	static inline void rprof_mutex_unlock(rprof_mutex* _mutex)	{
		LeaveCriticalSection(_mutex);
	}

#elif RPROF_PLATFORM_LINUX || RPROF_PLATFORM_OSX || RPROF_PLATFORM_ANDROID || RPROF_PLATFORM_EMSCRIPTEN
	typedef pthread_mutex_t rprof_mutex;

	static inline void rprof_mutex_init(rprof_mutex* _mutex) {
		pthread_mutex_init(_mutex, NULL);
	}

	static inline void rprof_mutex_destroy(rprof_mutex* _mutex) {
		pthread_mutex_destroy(_mutex);
	}

	static inline void rprof_mutex_lock(rprof_mutex* _mutex) {
		pthread_mutex_lock(_mutex);
	}

	static inline int rprof_mutex_trylock(rprof_mutex* _mutex) {
		return pthread_mutex_trylock(_mutex);
	}

	static inline void rprof_mutex_unlock(rprof_mutex* _mutex) {
		pthread_mutex_unlock(_mutex);
	}
	
#elif RPROF_PLATFORM_PS3
	typedef sys_lwmutex_t rprof_mutex;

	static inline void rprof_mutex_init(rprof_mutex* _mutex) {
		sys_lwmutex_attribute_t	mutexAttr;
		sys_lwmutex_attribute_initialize(mutexAttr);
		mutexAttr.attr_recursive = SYS_SYNC_RECURSIVE;
		sys_lwmutex_create(_mutex, &mutexAttr);
	}

	static inline void rprof_mutex_destroy(rprof_mutex* _mutex) {
		sys_lwmutex_destroy(_mutex);
	}

	static inline void rprof_mutex_lock(rprof_mutex* _mutex) {
		sys_lwmutex_lock(_mutex, 0);
	}

	static inline int rprof_mutex_trylock(rprof_mutex* _mutex) {
		return (sys_lwmutex_trylock(_mutex) == CELL_OK) ? 0 : 1;
	}

	static inline void rprof_mutex_unlock(rprof_mutex* _mutex) {
		sys_lwmutex_unlock(_mutex);
	}

#elif RPROF_PLATFORM_PS4
	typedef ScePthreadMutex rprof_mutex;

	static inline void rprof_mutex_init(rprof_mutex* _mutex) {
		ScePthreadMutexattr mutexAttr;
		scePthreadMutexattrInit(&mutexAttr);
		scePthreadMutexattrSettype(&mutexAttr, SCE_PTHREAD_MUTEX_RECURSIVE);
		scePthreadMutexInit(_mutex, &mutexAttr, 0);
		scePthreadMutexattrDestroy(&mutexAttr);
	}

	static inline void rprof_mutex_destroy(rprof_mutex* _mutex) {
		scePthreadMutexDestroy(_mutex);
	}

	static inline void rprof_mutex_lock(rprof_mutex* _mutex) {
		scePthreadMutexLock(_mutex);
	}

	static inline int rprof_mutex_trylock(rprof_mutex* _mutex) {
		return (scePthreadMutexTrylock(_mutex) == 0) ? 0 : 1;
	}

	static inline void rprof_mutex_unlock(rprof_mutex* _mutex) {
		scePthreadMutexUnlock(_mutex);
	}
	
#endif

	class Mutex
	{
		rprof_mutex m_mutex;

		Mutex(const Mutex& _rhs);
		Mutex& operator=(const Mutex& _rhs);
		
	public:

		inline Mutex()
		{
			rprof_mutex_init(&m_mutex);
		}

		inline ~Mutex() 
		{
			rprof_mutex_destroy(&m_mutex);
		}

		inline void lock()
		{
			rprof_mutex_lock(&m_mutex);
		}

		inline void unlock()
		{
			rprof_mutex_unlock(&m_mutex);
		}

		inline bool tryLock()
		{
			return (rprof_mutex_trylock(&m_mutex) == 0);
		}
	};

	class ScopedMutexLocker
	{
		Mutex& m_mutex;

		ScopedMutexLocker();
		ScopedMutexLocker(const ScopedMutexLocker&);
		ScopedMutexLocker& operator = (const ScopedMutexLocker&);

	public:

		inline ScopedMutexLocker(Mutex& _mutex) :
			m_mutex(_mutex)
		{
			m_mutex.lock();
		}

		inline ~ScopedMutexLocker()
		{
			m_mutex.unlock();
		}
	};

} // namespace rprof

#endif // RPROF_MUTEX_H

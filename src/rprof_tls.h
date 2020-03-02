/*
 * Copyright (c) 2020 by Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#ifndef RPROF_TLS_H
#define RPROF_TLS_H

#include "../inc/rprof.h"
#include "rprof_config.h"

#if RPROF_PLATFORM_WINDOWS || RPROF_PLATFORM_XBOXONE

namespace rprof {

	static inline uint32_t tlsAllocate()
	{
		return (uint32_t)TlsAlloc();
	}

	static inline void tlsSetValue(uint32_t _handle, void* _value)
	{
		TlsSetValue(_handle, _value);
	}

	static inline void* tlsGetValue(uint32_t _handle)
	{
		return TlsGetValue(_handle);
	}

	static inline void tlsFree(uint32_t _handle)
	{
		TlsFree(_handle);
	}

#elif RPROF_PLATFORM_PS4

namespace rprof {

	static inline uint32_t tlsAllocate()
	{
		ScePthreadKey handle;
		scePthreadKeyCreate(&handle, 0);
		return handle;
	}

	static inline void tlsSetValue(uint32_t _handle, void* _value)
	{
		scePthreadSetspecific(_handle, _value);
	}

	static inline void* tlsGetValue(uint32_t _handle)
	{
		return scePthreadGetspecific(_handle);
	}

	static inline void tlsFree(uint32_t _handle)
	{
		scePthreadKeyDelete(_handle);
	}

#elif RPROF_PLATFORM_POSIX

namespace rprof {

	static inline uint32_t tlsAllocate()
	{
		uint32_t handle;
		pthread_key_create((pthread_key_t*)&handle, 0);
		return handle;
	}

	static inline void tlsSetValue(uint32_t _handle, void* _value)
	{
		pthread_setspecific(_handle, _value);
	}

	static inline void* tlsGetValue(uint32_t _handle)
	{
		return pthread_getspecific(_handle);
	}

	static inline void tlsFree(uint32_t _handle)
	{
		pthread_key_delete(_handle);
	}

#else
	#error "Unsupported platform!"
#endif

} // namespace rprof

#endif // RPROF_TLS_H

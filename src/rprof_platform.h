/*
 * Copyright 2023 Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#ifndef RPROF_PLATFORM_H
#define RPROF_PLATFORM_H

#include "../inc/rprof.h"
#include "rprof_config.h"

/*--------------------------------------------------------------------------
 * Platforms
 *------------------------------------------------------------------------*/
#define RPROF_PLATFORM_WINDOWS		0
#define RPROF_PLATFORM_LINUX		0
#define RPROF_PLATFORM_IOS			0
#define RPROF_PLATFORM_OSX			0
#define RPROF_PLATFORM_PS3			0
#define RPROF_PLATFORM_PS4			0
#define RPROF_PLATFORM_ANDROID		0
#define RPROF_PLATFORM_XBOXONE		0
#define RPROF_PLATFORM_EMSCRIPTEN	0
#define RPROF_PLATFORM_SWITCH		0

/*--------------------------------------------------------------------------
 * Detect platform
 *------------------------------------------------------------------------*/
#if defined(_DURANGO) || defined(_XBOX_ONE)
#undef  RPROF_PLATFORM_XBOXONE
#define RPROF_PLATFORM_XBOXONE		1
#elif defined(_WIN32) || defined(_WIN64) || defined(__WINDOWS__)
#undef  RPROF_PLATFORM_WINDOWS
#define RPROF_PLATFORM_WINDOWS		1
#elif defined(__ANDROID__)
#undef  RPROF_PLATFORM_ANDROID
#define RPROF_PLATFORM_ANDROID		1
#elif defined(__linux__) || defined(linux)
#undef  RPROF_PLATFORM_LINUX
#define RPROF_PLATFORM_LINUX		1
#elif defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__)
#undef  RPROF_PLATFORM_IOS
#define RPROF_PLATFORM_IOS			1
#elif defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
#undef  RPROF_PLATFORM_OSX
#define RPROF_PLATFORM_OSX			1
#elif defined(__CELLOS_LV2__)
#undef  RPROF_PLATFORM_PS3
#define RPROF_PLATFORM_PS3			1
#elif defined(__ORBIS__)
#undef  RPROF_PLATFORM_PS4
#define RPROF_PLATFORM_PS4			1
#elif defined(__EMSCRIPTEN__)
#undef  RPROF_PLATFORM_EMSCRIPTEN
#define RPROF_PLATFORM_EMSCRIPTEN	1
#elif defined(__NINTENDO__)
#undef  RPROF_PLATFORM_SWITCH
#define RPROF_PLATFORM_SWITCH		1
#else
#error "Platform not supported!"
#endif

#define RPROF_PLATFORM_POSIX (	RPROF_PLATFORM_LINUX		|| \
								RPROF_PLATFORM_OSX			|| \
								RPROF_PLATFORM_ANDROID		|| \
								RPROF_PLATFORM_IOS			|| \
								RPROF_PLATFORM_PS4			|| \
								RPROF_PLATFORM_EMSCRIPTEN	|| \
								RPROF_PLATFORM_SWITCH		|| \
								0) 

/*--------------------------------------------------------------------------
 * Platform specific headers
 *------------------------------------------------------------------------*/
#if RPROF_PLATFORM_XBOXONE
	#include <windows.h>
#elif RPROF_PLATFORM_WINDOWS
	#define WINDOWS_LEAN_AND_MEAN
	#ifndef _WIN32_WINNT
		#define _WIN32_WINNT 0x601
	#endif
	#include <windows.h>
#elif RPROF_PLATFORM_PS3
	#include <sys/ppu_thread.h>
	#include <sys/sys_time.h>
#elif RPROF_PLATFORM_PS4
	#define _SYS__STDINT_H_
	#include <kernel.h>
#elif RPROF_PLATFORM_ANDROID
	#include <pthread.h>
	#include <time.h>
#elif RPROF_PLATFORM_EMSCRIPTEN
	#include <pthread.h>
	#include <time.h>
	#include <emscripten.h>
#elif RPROF_PLATFORM_SWITCH
	#include <pthread.h>
	#include <nn/os/os_Tick.h>
#elif RPROF_PLATFORM_POSIX
       #if RPROF_PLATFORM_LINUX
               #include <sys/syscall.h>
       #endif
	#include <unistd.h>	 // syscall
	#include <pthread.h>
	#include <sys/time.h>
#else
	#error "Unsupported platform!"
#endif

/*--------------------------------------------------------------------------*/
static inline uint64_t getThreadID()
{
#if RPROF_PLATFORM_WINDOWS || RPROF_PLATFORM_XBOXONE
	return (uint64_t)GetCurrentThreadId();
#elif RPROF_PLATFORM_LINUX
	return (uint64_t)syscall(SYS_gettid);
#elif RPROF_PLATFORM_IOS || RPROF_PLATFORM_OSX
	return (mach_port_t)::pthread_mach_thread_np(pthread_self() );
#elif RPROF_PLATFORM_PS3
	sys_ppu_thread_t tid;
	sys_ppu_thread_get_id(&tid);
	return (uint64_t)tid;
#elif RPROF_PLATFORM_PS4
	return (uint64_t)scePthreadSelf();
#elif RPROF_PLATFORM_ANDROID || RPROF_PLATFORM_EMSCRIPTEN || RPROF_PLATFORM_SWITCH
	return pthread_self();
#else
	#error "Unsupported platform!"
#endif
}

/*--------------------------------------------------------------------------*/
static inline uint8_t getPlatformID()
{
#if   RPROF_PLATFORM_WINDOWS
	return 1;
#elif RPROF_PLATFORM_LINUX
	return 2;
#elif RPROF_PLATFORM_IOS
	return 3;
#elif RPROF_PLATFORM_OSX
	return 4;
#elif RPROF_PLATFORM_PS3
	return 5;
#elif RPROF_PLATFORM_PS4
	return 6;
#elif RPROF_PLATFORM_ANDROID
	return 7;
#elif RPROF_PLATFORM_XBOXONE
	return 8;
#elif RPROF_PLATFORM_EMSCRIPTEN
	return 9;
#elif RPROF_PLATFORM_SWITCH
	return 10;
#else
	return 0xff;
#endif
}

/*--------------------------------------------------------------------------*/
static inline const char* getPlatformName(uint8_t _platformID)
{
	switch (_platformID)
	{
	case  1: return "Windows";
	case  2: return "Linux";
	case  3: return "iOS";
	case  4: return "OSX";
	case  5: return "PlayStation 3";
	case  6: return "PlayStation 4";
	case  7: return "Android";
	case  8: return "XboxOne";
	case  9: return "WebGL";
	case 10: return "Nintendo Switch";
	default: return "Unknown platform";
	};
}

#endif // RPROF_PLATFORM_H

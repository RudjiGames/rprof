/*
 * Copyright (c) 2020 by Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include "../inc/rprof.h"
#include "rprof_config.h"
#include "rprof_context.h"

#include <unordered_map>
#include <vector>

#include "../3rd/lz4-r191/lz4.h"
#if !RPROF_LZ4_NO_DEFINE
#include "../3rd/lz4-r191/lz4.c"
#endif

extern "C" uint64_t rprofGetClockFrequency();

/*--------------------------------------------------------------------------
 * Data load/save functions
 *------------------------------------------------------------------------*/

template <typename T>
static inline void writeVar(uint8_t*& _buffer, T _var)
{
	memcpy(_buffer, &_var, sizeof(T));
	_buffer += sizeof(T);
}

static inline void writeStr(uint8_t*& _buffer, const char* _str)
{
	uint32_t len = (uint32_t)strlen(_str);
	writeVar(_buffer, len);
	memcpy(_buffer, _str, len);
	_buffer += len;
}

template <typename T>
static inline void readVar(uint8_t*& _buffer, T& _var)
{
	memcpy(&_var, _buffer, sizeof(T));
	_buffer += sizeof(T);
}
	
static inline char* readString(uint8_t*& _buffer)
{
	uint32_t len;
	readVar(_buffer, len);
	char* str = new char[len+1];
	memcpy(str, _buffer, len);
	str[len] = 0;
	_buffer += len;
	return str;
}

const char* duplicateString(const char* _str)
{
	char* str = new char[strlen(_str)+1];
	strcpy(str, _str);
	return str;
}

struct StringStore
{
	typedef std::unordered_map<std::string, uint32_t> StringToIndexType;
	typedef std::unordered_map<uint32_t, std::string> IndexToStringType;

	uint32_t			m_totalSize;
	StringToIndexType	m_stringIndexMap;
	IndexToStringType	m_strings;

	StringStore()
		: m_totalSize(0)
	{
	}

	void addString(const char* _str)
	{
		StringToIndexType::iterator it = m_stringIndexMap.find(_str);
		if (it == m_stringIndexMap.end())
		{
			uint32_t index = (uint32_t)m_stringIndexMap.size();
			m_totalSize				+= 4 + (uint32_t)strlen(_str);	// see writeStr for details
			m_stringIndexMap[_str]	 = index;
			m_strings[index]		 = _str;
		}
	}

	uint32_t getString(const char* _str)
	{
		return m_stringIndexMap[_str];
	}
};

/*--------------------------------------------------------------------------
 * API functions
 *------------------------------------------------------------------------*/

rprof::ProfilerContext*	g_context = 0;

extern "C" {

	void rprofInit()
	{
		g_context = new rprof::ProfilerContext();
	}

	void rprofShutDown()
	{
		delete g_context;
		g_context = 0;
	}

	void rprofSetThreshold(float _ms, int _level)
	{
		g_context->setThreshold(_ms, _level);
	}

	void rprofRegisterThread(const char* _name)
	{
		g_context->registerThread(getThreadID(), _name);
	}

	void rprofUnregisterThread(uint64_t _threadID)
	{
		g_context->unregisterThread(_threadID);
	}

	void rprofBeginFrame()
	{
		g_context->beginFrame();
	}

	uintptr_t rprofBeginScope(const char* _file, int _line, const char* _name)
	{
		return (uintptr_t)g_context->beginScope(_file, _line, _name);
	}

	void rprofEndScope(uintptr_t _scopeHandle)
	{
		g_context->endScope((ProfilerScope*)_scopeHandle);
	}

	int rprofIsPaused()
	{
		return g_context->isPaused() ? 1 : 0;
	}

	int rprofWasThresholdCrossed()
	{
		return g_context->wasThresholdCrossed() ? 1 : 0;
	}

	void rprofSetPaused(int _paused)
	{
		return g_context->setPaused(_paused != 0);
	}

	void rprofGetFrame(ProfilerFrame* _data)
	{
		g_context->getFrameData(_data);

		// clamp scopes crossing frame boundary
		const uint32_t numScopes = _data->m_numScopes;
		for (uint32_t i=0; i<numScopes; ++i)
		{
			ProfilerScope& cs = _data->m_scopes[i];

			if (cs.m_start == cs.m_end)
			{
				cs.m_end = _data->m_endtime;
				if (cs.m_start < _data->m_startTime)
					cs.m_start = _data->m_startTime;
			}
		}
	}

	int rprofSave(ProfilerFrame* _data, void* _buffer, size_t _bufferSize)
	{
		// fill string data
		StringStore strStore;
		for (uint32_t i=0; i<_data->m_numScopes; ++i)
		{
			ProfilerScope& scope = _data->m_scopes[i];
			strStore.addString(scope.m_name);
			strStore.addString(scope.m_file);
		}
		for (uint32_t i=0; i<_data->m_numThreads; ++i)
		{
			strStore.addString(_data->m_threads[i].m_name);
		}

		// calc data size
		uint32_t totalSize =	_data->m_numScopes  * sizeof(ProfilerScope)  +
								_data->m_numThreads * sizeof(ProfilerThread) +
								sizeof(ProfilerFrame) +
								strStore.m_totalSize;

		uint8_t* buffer = new uint8_t[totalSize];
		uint8_t* bufPtr = buffer;

		writeVar(buffer, _data->m_startTime);
		writeVar(buffer, _data->m_endtime);
		writeVar(buffer, _data->m_prevFrameTime);
		writeVar(buffer, _data->m_platformID);
		writeVar(buffer, rprofGetClockFrequency());

		// write scopes
		writeVar(buffer, _data->m_numScopes);
		for (uint32_t i=0; i<_data->m_numScopes; ++i)
		{
			ProfilerScope& scope = _data->m_scopes[i];
			writeVar(buffer, scope.m_start);
			writeVar(buffer, scope.m_end);
			writeVar(buffer, scope.m_threadID);
			writeVar(buffer, strStore.getString(scope.m_name));
			writeVar(buffer, strStore.getString(scope.m_file));
			writeVar(buffer, scope.m_line);
			writeVar(buffer, scope.m_level);
		}

		// write thread info
		writeVar(buffer, _data->m_numThreads);
		for (uint32_t i=0; i<_data->m_numThreads; ++i)
		{
			ProfilerThread& t = _data->m_threads[i];
			writeVar(buffer, t.m_threadID);
			writeVar(buffer, strStore.getString(t.m_name));
		}

		// write string data
		uint32_t numStrings = (uint32_t)strStore.m_strings.size();
		writeVar(buffer, numStrings);

		for (uint32_t i=0; i<strStore.m_strings.size(); ++i)
			writeStr(buffer, strStore.m_strings[i].c_str());

		int compSize = LZ4_compress_default((const char*)bufPtr, (char*)_buffer, (int)(buffer - bufPtr), (int)_bufferSize);
		delete[] bufPtr;
		return compSize;
	}

	void rprofLoad(ProfilerFrame* _data, void* _buffer, size_t _bufferSize)
	{
		size_t		bufferSize	= _bufferSize;
		uint8_t*	buffer		= 0;
		uint8_t*	bufferPtr;

		int decomp = -1;
		do 
		{
			delete[] buffer;
			bufferSize *= 2;
			buffer = new uint8_t[bufferSize];
			decomp = LZ4_decompress_safe((const char*)_buffer, (char*)buffer, (int)_bufferSize, (int)bufferSize);

		} while (decomp < 0);

		bufferPtr = buffer;

		uint32_t strIdx;

		readVar(buffer, _data->m_startTime);
		readVar(buffer, _data->m_endtime);
		readVar(buffer, _data->m_prevFrameTime);
		readVar(buffer, _data->m_platformID);
		readVar(buffer, _data->m_CPUFrequency);

		// read scopes
		readVar(buffer, _data->m_numScopes);

		_data->m_scopes			= new ProfilerScope[_data->m_numScopes * 2]; // extra space for viewer - m_scopesStats
		_data->m_scopesStats	= &_data->m_scopes[_data->m_numScopes];
		_data->m_scopeStatsInfo	= new ProfilerScopeStats[_data->m_numScopes * 2];

		for (uint32_t i=0; i<_data->m_numScopes*2; ++i)
			_data->m_scopes[i].m_stats = &_data->m_scopeStatsInfo[i];

		for (uint32_t i=0; i<_data->m_numScopes; ++i)
		{
			ProfilerScope& scope = _data->m_scopes[i];
			readVar(buffer, scope.m_start);
			readVar(buffer, scope.m_end);
			readVar(buffer, scope.m_threadID);
			readVar(buffer, strIdx);
			scope.m_name = (const char*)(uintptr_t)strIdx;
			readVar(buffer, strIdx);
			scope.m_file = (const char*)(uintptr_t)strIdx;
			readVar(buffer, scope.m_line);
			readVar(buffer, scope.m_level);

			scope.m_stats->m_inclusiveTime	= scope.m_end - scope.m_start;
			scope.m_stats->m_exclusiveTime	= scope.m_stats->m_inclusiveTime;
			scope.m_stats->m_occurences		= 0;
		}

		// read thread info
		readVar(buffer, _data->m_numThreads);
		_data->m_threads = new ProfilerThread[_data->m_numThreads];
		for (uint32_t i=0; i<_data->m_numThreads; ++i)
		{
			ProfilerThread& t = _data->m_threads[i];
			readVar(buffer, t.m_threadID);
			readVar(buffer, strIdx);
			t.m_name = (const char*)(uintptr_t)strIdx;
		}

		// read string data
		uint32_t numStrings;
		readVar(buffer, numStrings);

		const char*	strings[RPROF_SCOPES_MAX];
		for (uint32_t i=0; i<numStrings; ++i)
			strings[i] = readString(buffer);

		for (uint32_t i=0; i<_data->m_numScopes; ++i)
		{
			ProfilerScope& scope = _data->m_scopes[i];
			uintptr_t idx = (uintptr_t)scope.m_name;
			scope.m_name = duplicateString(strings[(uint32_t)idx]);

			idx = (uintptr_t)scope.m_file;
			scope.m_file = duplicateString(strings[(uint32_t)idx]);
		}

		for (uint32_t i=0; i<_data->m_numThreads; ++i)
		{
			ProfilerThread& t = _data->m_threads[i];
			uintptr_t idx = (uintptr_t)t.m_name;
			t.m_name = duplicateString(strings[(uint32_t)idx]);
		}

		for (uint32_t i=0; i<numStrings; ++i)
			delete[] strings[i];

		delete[] bufferPtr;

		// process frame data

		for (uint32_t i=0; i<_data->m_numScopes; ++i)
		for (uint32_t j=0; j<_data->m_numScopes; ++j)
		{
			ProfilerScope& scopeI = _data->m_scopes[i];
			ProfilerScope& scopeJ = _data->m_scopes[j];

			if ((scopeJ.m_start > scopeI.m_start) && (scopeJ.m_end < scopeI.m_end) &&
				(scopeJ.m_level == scopeI.m_level + 1) && (scopeJ.m_threadID == scopeI.m_threadID))
				scopeI.m_stats->m_exclusiveTime -= scopeJ.m_stats->m_inclusiveTime;
		}

		_data->m_numScopesStats	= 0;

		for (uint32_t i=0; i<_data->m_numScopes; ++i)
		{
			ProfilerScope& scopeI = _data->m_scopes[i];

			scopeI.m_stats->m_inclusiveTimeTotal = scopeI.m_stats->m_inclusiveTime;
			scopeI.m_stats->m_exclusiveTimeTotal = scopeI.m_stats->m_exclusiveTime;

			int foundIndex = -1;
			for (uint32_t j=0; j<_data->m_numScopesStats; ++j)
			{
				ProfilerScope& scopeJ = _data->m_scopesStats[j];
				if (strcmp(scopeI.m_name, scopeJ.m_name) == 0)
				{
					foundIndex = j;
					break;
				}
			}

			if (foundIndex == -1)
			{
				int index = _data->m_numScopesStats++;
				ProfilerScope& scope = _data->m_scopesStats[index];
				scope						= scopeI;
				scope.m_stats->m_occurences	= 1;
			}
			else
			{
				ProfilerScope& scope = _data->m_scopesStats[foundIndex];
				scope.m_stats->m_inclusiveTimeTotal += scopeI.m_stats->m_inclusiveTime;
				scope.m_stats->m_exclusiveTimeTotal += scopeI.m_stats->m_exclusiveTime;
				scope.m_stats->m_occurences++;
			}
		}
	}

	void rprofLoadTimeOnly(float* _time, void* _buffer, size_t _bufferSize)
	{
		size_t		bufferSize = _bufferSize;
		uint8_t*	buffer = 0;
		uint8_t*	bufferPtr;

		int decomp = -1;
		do
		{
			delete[] buffer;
			bufferSize *= 2;
			buffer = new uint8_t[bufferSize];
			decomp = LZ4_decompress_safe((const char*)_buffer, (char*)buffer, (int)_bufferSize, (int)bufferSize);

		} while (decomp < 0);

		bufferPtr = buffer;

		uint64_t startTime;
		uint64_t endtime;
		uint8_t  dummy8;
		uint64_t frequency;

		readVar(buffer, startTime);
		readVar(buffer, endtime);
		readVar(buffer, frequency);	// dummy
		readVar(buffer, dummy8);	// dummy
		readVar(buffer, frequency);
		*_time = rprofClock2ms(endtime - startTime, frequency);

		delete[] buffer;
	}

	void rprofRelease(ProfilerFrame* _data)
	{
		for (uint32_t i=0; i<_data->m_numScopes; ++i)
		{
			ProfilerScope& scope = _data->m_scopes[i];
			delete[] scope.m_name;
			delete[] scope.m_file;
		}

		for (uint32_t i=0; i<_data->m_numThreads; ++i)
		{
			ProfilerThread& t = _data->m_threads[i];
			delete[] t.m_name;
		}

		delete[] _data->m_scopes;
		delete[] _data->m_threads;
		delete[] _data->m_scopeStatsInfo;
	}

	uint64_t rprofGetClock()
	{
#if   RPROF_PLATFORM_WINDOWS
	#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
		uint64_t q = __rdtsc();
	#else
		LARGE_INTEGER li;
		QueryPerformanceCounter(&li);
		int64_t q = li.QuadPart;
	#endif
#elif RPROF_PLATFORM_XBOXONE
		LARGE_INTEGER li;
		QueryPerformanceCounter(&li);
		int64_t q = li.QuadPart;
#elif RPROF_PLATFORM_PS4
		int64_t q = sceKernelReadTsc();
#elif RPROF_PLATFORM_ANDROID
		int64_t q = ::clock();
#elif RPROF_PLATFORM_EMSCRIPTEN
		int64_t q = (int64_t)(emscripten_get_now() * 1000.0);
#else
		struct timeval now;
		gettimeofday(&now, 0);
		int64_t q = now.tv_sec * 1000000 + now.tv_usec;
#endif
		return q;
	}

	uint64_t rprofGetClockFrequency()
	{
#if   RPROF_PLATFORM_WINDOWS
	#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
		static uint64_t frequency = 1;
		static bool initialized = false;
		if (!initialized)
		{
			LARGE_INTEGER li1, li2;
			QueryPerformanceCounter(&li1);
			uint64_t tsc1 = __rdtsc();
			for (int i=0; i<230000000; ++i);
			uint64_t tsc2 = __rdtsc();
			QueryPerformanceCounter(&li2);

			LARGE_INTEGER lif;
			QueryPerformanceFrequency(&lif);
			uint64_t time = ((li2.QuadPart - li1.QuadPart) * 1000) / lif.QuadPart;
			frequency = (uint64_t)(1000 * ((tsc2-tsc1)/time));
			initialized = true;
		}
		return frequency;
	#else
		LARGE_INTEGER li;
		QueryPerformanceFrequency(&li);
		return li.QuadPart;
	#endif
#elif RPROF_PLATFORM_XBOXONE
		LARGE_INTEGER li;
		QueryPerformanceFrequency(&li);
		return li.QuadPart;
#elif RPROF_PLATFORM_ANDROID
		return CLOCKS_PER_SEC;
#elif RPROF_PLATFORM_PS4
		return sceKernelGetTscFrequency();
#else
		return 1000000;
#endif
	}

	float rprofClock2ms(uint64_t _clock, uint64_t _frequency)
	{
		return (float(_clock) / float(_frequency)) * 1000.0f;
	}

	const char* rprofGetPlatformName(uint8_t _platformID)
	{
		return getPlatformName(_platformID);
	}

} // extern "C"

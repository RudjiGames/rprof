/*
 * Copyright 2025 Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include "../inc/rprof.h"
#include "rprof_config.h"
#include "rprof_context.h"

#include "../3rd/lz4-r191/lz4.h"
#if !RPROF_LZ4_NO_DEFINE
#include "../3rd/lz4-r191/lz4.c"
#endif

extern "C" uint64_t rprofGetClockFrequency();

/*--------------------------------------------------------------------------
 * Data load/save functions
 *------------------------------------------------------------------------*/

static inline void memoryCopy(void* _dst, const void* _src, size_t _len)
{
	const uint8_t* src = (const uint8_t*)_src;
	uint8_t* dst = (uint8_t*)_dst;
	for (size_t i=0; i<_len; ++i)
		dst[i] = src[i];
}

template <typename T>
static inline void writeVar(uint8_t*& _buffer, T _var)
{
	memoryCopy(_buffer, &_var, sizeof(T));
	_buffer += sizeof(T);
}

static inline void writeStr(uint8_t*& _buffer, const char* _str)
{
	uint32_t len = (uint32_t)strlen(_str);
	writeVar(_buffer, len);
	memoryCopy(_buffer, _str, len);
	_buffer += len;
}

template <typename T>
static inline void readVar(uint8_t*& _buffer, T& _var)
{
	memoryCopy(&_var, _buffer, sizeof(T));
	_buffer += sizeof(T);
}
	
// bounds-checked variants used when parsing untrusted capture buffers

template <typename T>
static inline bool readVarSafe(uint8_t*& _buffer, const uint8_t* _end, T& _var)
{
	if ((size_t)(_end - _buffer) < sizeof(T))
		return false;
	memoryCopy(&_var, _buffer, sizeof(T));
	_buffer += sizeof(T);
	return true;
}

static inline char* readStringSafe(uint8_t*& _buffer, const uint8_t* _end)
{
	uint32_t len;
	if (!readVarSafe(_buffer, _end, len))
		return 0;
	if ((size_t)(_end - _buffer) < len)
		return 0;
	char* str = new char[len+1];
	memoryCopy(str, _buffer, len);
	str[len] = 0;
	_buffer += len;
	return str;
}

const char* duplicateString(const char* _str)
{
	if (!_str)
		return nullptr;
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
		// pre-calibrate the clock frequency now, outside of any lock, so the
		// first rprofBeginFrame() does not stall while holding the mutex
		rprofGetClockFrequency();
	}

	void rprofShutDown()
	{
		delete g_context;
		g_context = 0;
	}

	void rprofSetThreshold(float _ms, int _level)
	{
		if (g_context)
			g_context->setThreshold(_ms, _level);
	}

	void rprofRegisterThread(const char* _name, uint64_t _threadID)
	{
		if (_threadID == 0)
			_threadID = getThreadID();

		if (g_context)
			g_context->registerThread(_threadID, _name);
	}

	void rprofUnregisterThread(uint64_t _threadID)
	{
		if (g_context)
			g_context->unregisterThread(_threadID);
	}

	void rprofBeginFrame()
	{
		if (g_context)
			g_context->beginFrame();
	}

	uintptr_t rprofBeginScope(const char* _file, int _line, const char* _name)
	{
		if (g_context)
			return (uintptr_t)g_context->beginScope(_file, _line, _name);
		return 0;
	}

	void rprofEndScope(uintptr_t _scopeHandle)
	{
		if (g_context)
			g_context->endScope((ProfilerScope*)_scopeHandle);
	}

	int rprofIsPaused()
	{
		return g_context && g_context->isPaused() ? 1 : 0;
	}

	int rprofWasThresholdCrossed()
	{
		return g_context && g_context->wasThresholdCrossed() ? 1 : 0;
	}

	void rprofSetPaused(int _paused)
	{
		if (g_context)
			g_context->setPaused(_paused != 0);
	}

	int  rprofGetFrame(ProfilerFrame* _data)
	{
		if (!g_context)
			return 0;

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

		return 1;
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

		// calc data size, overestimate to be safe
		uint32_t maxTotalSize =	_data->m_numScopes  * sizeof(ProfilerScope)  +
								_data->m_numThreads * sizeof(ProfilerThread) +
								sizeof(ProfilerFrame) +
								strStore.m_totalSize;

		uint8_t* buffer = new uint8_t[maxTotalSize];
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

		} while ((decomp < 0) && (bufferSize <= RPROF_LZ4_BUFFER_MAX_SIZE));

		bufferPtr = buffer;

		// leave the frame in a safe, releasable state until parsing succeeds
		_data->m_numScopes		= 0;
		_data->m_numScopesStats	= 0;
		_data->m_numThreads		= 0;
		_data->m_scopes			= 0;
		_data->m_scopesStats	= 0;
		_data->m_scopeStatsInfo	= 0;
		_data->m_threads		= 0;

		// decompression never succeeded - nothing we can safely parse
		if (decomp < 0)
		{
			delete[] bufferPtr;
			return;
		}

		const uint8_t* bufferEnd = bufferPtr + decomp;

		uint32_t numScopes	= 0;
		uint32_t numThreads	= 0;
		uint32_t numStrings	= 0;
		uint32_t strIdx;
		bool ok = true;

		ok = ok && readVarSafe(buffer, bufferEnd, _data->m_startTime);
		ok = ok && readVarSafe(buffer, bufferEnd, _data->m_endtime);
		ok = ok && readVarSafe(buffer, bufferEnd, _data->m_prevFrameTime);
		ok = ok && readVarSafe(buffer, bufferEnd, _data->m_platformID);
		ok = ok && readVarSafe(buffer, bufferEnd, _data->m_CPUFrequency);

		// read scopes
		ok = ok && readVarSafe(buffer, bufferEnd, numScopes);

		// a serialized scope is 40 bytes; reject counts that cannot possibly fit
		// in the decompressed data to avoid integer overflow / over-allocation
		if (ok && (numScopes > (size_t)(bufferEnd - buffer) / 40))
			ok = false;

		if (!ok)
		{
			delete[] bufferPtr;
			return;
		}

		_data->m_numScopes		= numScopes;
		_data->m_scopes			= new ProfilerScope[(size_t)numScopes * 2]; // extra space for viewer - m_scopesStats
		_data->m_scopesStats	= &_data->m_scopes[numScopes];
		_data->m_scopeStatsInfo	= new ProfilerScopeStats[(size_t)numScopes * 2];

		for (uint32_t i=0; i<numScopes*2; ++i)
			_data->m_scopes[i].m_stats = &_data->m_scopeStatsInfo[i];

		for (uint32_t i=0; i<numScopes && ok; ++i)
		{
			ProfilerScope& scope = _data->m_scopes[i];
			ok = ok && readVarSafe(buffer, bufferEnd, scope.m_start);
			ok = ok && readVarSafe(buffer, bufferEnd, scope.m_end);
			ok = ok && readVarSafe(buffer, bufferEnd, scope.m_threadID);
			ok = ok && readVarSafe(buffer, bufferEnd, strIdx);
			scope.m_name = (const char*)(uintptr_t)strIdx;
			ok = ok && readVarSafe(buffer, bufferEnd, strIdx);
			scope.m_file = (const char*)(uintptr_t)strIdx;
			ok = ok && readVarSafe(buffer, bufferEnd, scope.m_line);
			ok = ok && readVarSafe(buffer, bufferEnd, scope.m_level);

			scope.m_stats->m_inclusiveTime	= scope.m_end - scope.m_start;
			scope.m_stats->m_exclusiveTime	= scope.m_stats->m_inclusiveTime;
			scope.m_stats->m_occurences		= 0;
		}

		// read thread info
		ok = ok && readVarSafe(buffer, bufferEnd, numThreads);

		// a serialized thread is 12 bytes
		if (ok && (numThreads > (size_t)(bufferEnd - buffer) / 12))
			ok = false;

		if (ok)
		{
			_data->m_numThreads	= numThreads;
			_data->m_threads	= new ProfilerThread[numThreads];
			for (uint32_t i=0; i<numThreads && ok; ++i)
			{
				ProfilerThread& t = _data->m_threads[i];
				ok = ok && readVarSafe(buffer, bufferEnd, t.m_threadID);
				ok = ok && readVarSafe(buffer, bufferEnd, strIdx);
				t.m_name = (const char*)(uintptr_t)strIdx;
			}
		}

		// read string data
		ok = ok && readVarSafe(buffer, bufferEnd, numStrings);

		// each string is at least a 4-byte length prefix
		if (ok && (numStrings > (size_t)(bufferEnd - buffer) / 4))
			ok = false;

		const char** strings = 0;
		if (ok)
		{
			strings = new const char*[numStrings];
			for (uint32_t i=0; i<numStrings; ++i)
				strings[i] = 0;
			for (uint32_t i=0; i<numStrings && ok; ++i)
			{
				strings[i] = readStringSafe(buffer, bufferEnd);
				if (!strings[i])
					ok = false;
			}
		}

		// bail out cleanly on a malformed buffer - at this point scope/thread
		// names still hold raw indices (not heap pointers), so we must not let
		// rprofRelease try to free them
		if (!ok)
		{
			if (strings)
			{
				for (uint32_t i=0; i<numStrings; ++i)
					delete[] strings[i];
				delete[] strings;
			}
			delete[] bufferPtr;

			delete[] _data->m_scopes;
			delete[] _data->m_threads;
			delete[] _data->m_scopeStatsInfo;
			_data->m_scopes			= 0;
			_data->m_scopesStats	= 0;
			_data->m_scopeStatsInfo	= 0;
			_data->m_threads		= 0;
			_data->m_numScopes		= 0;
			_data->m_numScopesStats	= 0;
			_data->m_numThreads		= 0;
			return;
		}

		for (uint32_t i=0; i<numScopes; ++i)
		{
			ProfilerScope& scope = _data->m_scopes[i];
			uintptr_t idx = (uintptr_t)scope.m_name;
			scope.m_name = duplicateString((idx < numStrings) ? strings[(uint32_t)idx] : "");

			idx = (uintptr_t)scope.m_file;
			scope.m_file = duplicateString((idx < numStrings) ? strings[(uint32_t)idx] : "");
		}

		for (uint32_t i=0; i<numThreads; ++i)
		{
			ProfilerThread& t = _data->m_threads[i];
			uintptr_t idx = (uintptr_t)t.m_name;
			t.m_name = duplicateString((idx < numStrings) ? strings[(uint32_t)idx] : "");
		}

		for (uint32_t i=0; i<numStrings; ++i)
			delete[] strings[i];

		delete[] strings;
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

				// the struct copy below clobbers m_stats, so preserve this
				// entry's own dedicated stats slot and copy the values into it
				// rather than aliasing the source scope's stats
				ProfilerScopeStats* stats	= scope.m_stats;
				scope						= scopeI;
				scope.m_stats				= stats;
				*scope.m_stats				= *scopeI.m_stats;
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

		int decomp = -1;
		do
		{
			delete[] buffer;
			bufferSize *= 2;
			buffer = new uint8_t[bufferSize];
			decomp = LZ4_decompress_safe((const char*)_buffer, (char*)buffer, (int)_bufferSize, (int)bufferSize);

		} while ((decomp < 0) && (bufferSize <= RPROF_LZ4_BUFFER_MAX_SIZE));

		uint64_t startTime = 0;
		uint64_t endtime = 0, prevFrameTime;
		uint32_t platformID;
		uint64_t frequency = 0;

		uint8_t* bufPtr = buffer;
		*_time = 0.0f;

		if (decomp >= 0)
		{
			const uint8_t* bufferEnd = bufPtr + decomp;
			bool ok = true;
			ok = ok && readVarSafe(buffer, bufferEnd, startTime);
			ok = ok && readVarSafe(buffer, bufferEnd, endtime);
			ok = ok && readVarSafe(buffer, bufferEnd, prevFrameTime);	// dummy
			ok = ok && readVarSafe(buffer, bufferEnd, platformID);		// dummy
			ok = ok && readVarSafe(buffer, bufferEnd, frequency);
			if (ok)
				*_time = rprofClock2ms(endtime - startTime, frequency);
		}

		delete[] bufPtr;
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
#elif RPROF_PLATFORM_SWITCH
		int64_t q = nn::os::GetSystemTick().GetInt64Value();
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
		// C++11 guarantees thread-safe, once-only initialization of a
		// function-local static, so the rdtsc calibration runs exactly once
		// even when called concurrently
		static const uint64_t frequency = []() -> uint64_t {
			LARGE_INTEGER li1, li2;
			QueryPerformanceCounter(&li1);
			uint64_t tsc1 = __rdtsc();
			Sleep(230);
			uint64_t tsc2 = __rdtsc();
			QueryPerformanceCounter(&li2);

			LARGE_INTEGER lif;
			QueryPerformanceFrequency(&lif);
			uint64_t time = ((li2.QuadPart - li1.QuadPart) * 1000) / lif.QuadPart;
			if (time == 0)
				return 1;
			return (uint64_t)(1000 * ((tsc2 - tsc1) / time));
		}();
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
#elif RPROF_PLATFORM_SWITCH
		return nn::os::GetSystemTickFrequency();
#else
		return 1000000;
#endif
	}

	float rprofClock2ms(uint64_t _clock, uint64_t _frequency)
	{
		return _frequency ? (float(_clock) / float(_frequency)) * 1000.0f : 0.0f;
	}

	const char* rprofGetPlatformName(uint8_t _platformID)
	{
		return getPlatformName(_platformID);
	}

} // extern "C"

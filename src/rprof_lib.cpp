/*
 * Copyright 2025 Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include "../inc/rprof.h"
#include "../inc/rprof_cstd.h"
#include "rprof_config.h"
#include "rprof_context.h"
#include "rprof_alloc.h"

#include <algorithm>

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
	uint32_t len = (uint32_t)rprofStrLen(_str);
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
	char* str = (char*)rprofAlloc(len+1);
	memoryCopy(str, _buffer, len);
	str[len] = 0;
	_buffer += len;
	return str;
}

const char* duplicateString(const char* _str)
{
	if (!_str)
		return nullptr;
	char* str = (char*)rprofAlloc(rprofStrLen(_str)+1);
	rprofStrCpy(str, _str);
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
			m_totalSize				+= 4 + (uint32_t)rprofStrLen(_str);	// see writeStr for details
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

// host provided allocator (see rprofSetAllocator)
static rprofAllocFn	g_allocFn	= 0;
static rprofFreeFn	g_freeFn	= 0;
static void*		g_allocUD	= 0;

extern "C" void* rprofAlloc(size_t _size)
{
	return g_allocFn ? g_allocFn(g_allocUD, _size) : 0;
}

extern "C" void rprofFree(void* _ptr)
{
	if (g_freeFn && _ptr)
		g_freeFn(g_allocUD, _ptr);
}

// orders scopes so a parent is immediately followed by its (properly nested)
// descendants: by thread, then start ascending, then end descending
struct SortNested
{
	bool operator()(const ProfilerScope& a, const ProfilerScope& b) const
	{
		if (a.m_threadID != b.m_threadID)	return a.m_threadID < b.m_threadID;
		if (a.m_start    != b.m_start)		return a.m_start    < b.m_start;
		return a.m_end > b.m_end;
	}
};

// groups scopes with identical names next to each other
struct SortName
{
	bool operator()(const ProfilerScope& a, const ProfilerScope& b) const
	{
		return rprofStrCmp(a.m_name, b.m_name) < 0;
	}
};

extern "C" {

	void rprofSetAllocator(rprofAllocFn _alloc, rprofFreeFn _free, void* _userData)
	{
		g_allocFn = _alloc;
		g_freeFn  = _free;
		g_allocUD = _userData;
	}

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

		uint8_t* buffer = (uint8_t*)rprofAlloc(maxTotalSize);
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
		rprofFree(bufPtr);
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
			rprofFree(buffer);
			bufferSize *= 2;
			buffer = (uint8_t*)rprofAlloc(bufferSize);
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
			rprofFree(bufferPtr);
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
			rprofFree(bufferPtr);
			return;
		}

		_data->m_numScopes		= numScopes;
		_data->m_scopes			= (ProfilerScope*)rprofAlloc(sizeof(ProfilerScope) * (size_t)numScopes * 2); // extra space for viewer - m_scopesStats
		_data->m_scopesStats	= &_data->m_scopes[numScopes];
		_data->m_scopeStatsInfo	= (ProfilerScopeStats*)rprofAlloc(sizeof(ProfilerScopeStats) * (size_t)numScopes * 2);

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
			_data->m_threads	= (ProfilerThread*)rprofAlloc(sizeof(ProfilerThread) * numThreads);
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
			strings = (const char**)rprofAlloc(sizeof(const char*) * numStrings);
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
					rprofFree((void*)strings[i]);
				rprofFree(strings);
			}
			rprofFree(bufferPtr);

			rprofFree(_data->m_scopes);
			rprofFree(_data->m_threads);
			rprofFree(_data->m_scopeStatsInfo);
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
			rprofFree((void*)strings[i]);

		rprofFree(strings);
		rprofFree(bufferPtr);

		// process frame data

		const uint32_t numScopesLocal = _data->m_numScopes;

		// --- exclusive time: subtract each scope's direct children -----------
		// Sort by (thread, start asc, end desc) so a scope's descendants are
		// contiguous after it and properly nested, then walk an ancestor stack
		// in a single pass instead of comparing every scope against every other.
		std::sort(&_data->m_scopes[0], &_data->m_scopes[numScopesLocal], SortNested());

		// ancestor stack of indices into m_scopes; nesting depth never exceeds
		// the scope count, and a captured frame holds at most RPROF_SCOPES_MAX
		static uint32_t s_stack[RPROF_SCOPES_MAX];
		uint32_t sp = 0;
		uint64_t stackThread = 0;

		for (uint32_t i=0; i<numScopesLocal; ++i)
		{
			ProfilerScope& s = _data->m_scopes[i];

			// scopes are grouped by thread - reset the stack on each new thread
			if (sp == 0 || s.m_threadID != stackThread)
			{
				sp = 0;
				stackThread = s.m_threadID;
			}

			// pop ancestors that ended before this scope started
			while (sp > 0 && _data->m_scopes[s_stack[sp-1]].m_end <= s.m_start)
				--sp;

			if (sp > 0)
			{
				ProfilerScope& parent = _data->m_scopes[s_stack[sp-1]];
				if ((parent.m_level + 1 == s.m_level) &&
					(s.m_start > parent.m_start) && (s.m_end < parent.m_end))
					parent.m_stats->m_exclusiveTime -= s.m_stats->m_inclusiveTime;
			}

			if (sp < RPROF_SCOPES_MAX)
				s_stack[sp++] = i;
		}

		// --- per-name stats: group instead of an O(n*unique) name scan -------
		// Sort by name so equal names are adjacent, then accumulate each run.
		std::sort(&_data->m_scopes[0], &_data->m_scopes[numScopesLocal], SortName());

		_data->m_numScopesStats	= 0;

		for (uint32_t i=0; i<numScopesLocal; )
		{
			ProfilerScope& scopeI = _data->m_scopes[i];

			int index = _data->m_numScopesStats++;
			ProfilerScope& stat = _data->m_scopesStats[index];

			// preserve this entry's own dedicated stats slot (the struct copy
			// below would otherwise alias the source scope's stats)
			ProfilerScopeStats* stats	= stat.m_stats;
			stat						= scopeI;
			stat.m_stats				= stats;
			stat.m_stats->m_inclusiveTimeTotal	= 0;
			stat.m_stats->m_exclusiveTimeTotal	= 0;
			stat.m_stats->m_occurences			= 0;

			uint32_t j = i;
			while ((j < numScopesLocal) && (rprofStrCmp(_data->m_scopes[j].m_name, scopeI.m_name) == 0))
			{
				ProfilerScope& sj = _data->m_scopes[j];
				sj.m_stats->m_inclusiveTimeTotal = sj.m_stats->m_inclusiveTime;
				sj.m_stats->m_exclusiveTimeTotal = sj.m_stats->m_exclusiveTime;

				stat.m_stats->m_inclusiveTimeTotal += sj.m_stats->m_inclusiveTime;
				stat.m_stats->m_exclusiveTimeTotal += sj.m_stats->m_exclusiveTime;
				stat.m_stats->m_occurences++;
				++j;
			}

			i = j;
		}
	}

	void rprofLoadTimeOnly(float* _time, void* _buffer, size_t _bufferSize)
	{
		size_t		bufferSize = _bufferSize;
		uint8_t*	buffer = 0;

		int decomp = -1;
		do
		{
			rprofFree(buffer);
			bufferSize *= 2;
			buffer = (uint8_t*)rprofAlloc(bufferSize);
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

		rprofFree(bufPtr);
	}

	void rprofRelease(ProfilerFrame* _data)
	{
		for (uint32_t i=0; i<_data->m_numScopes; ++i)
		{
			ProfilerScope& scope = _data->m_scopes[i];
			rprofFree((void*)scope.m_name);
			rprofFree((void*)scope.m_file);
		}

		for (uint32_t i=0; i<_data->m_numThreads; ++i)
		{
			ProfilerThread& t = _data->m_threads[i];
			rprofFree((void*)t.m_name);
		}

		rprofFree(_data->m_scopes);
		rprofFree(_data->m_threads);
		rprofFree(_data->m_scopeStatsInfo);
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

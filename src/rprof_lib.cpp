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

// deduplicating string table used when saving: an open addressing hash
// table allocated through the host allocator (no CRT heap, no string copies)
struct StringTable
{
	const char**	m_slots;		// hash slot -> string, 0 when empty
	uint32_t*		m_slotIndex;	// hash slot -> string index
	const char**	m_strings;		// string index -> string, insertion order
	const char*		m_ptrCache[256];		// direct mapped pointer -> index cache;
	uint32_t		m_ptrCacheIndex[256];	// scopes from one file share a pointer
	uint32_t		m_mask;
	uint32_t		m_count;
	uint32_t		m_totalSize;

	StringTable()
		: m_slots(0)
		, m_slotIndex(0)
		, m_strings(0)
		, m_mask(0)
		, m_count(0)
		, m_totalSize(0)
	{
		for (int i=0; i<256; ++i)
			m_ptrCache[i] = 0;
	}

	~StringTable()
	{
		rprofFree(m_slots);
		rprofFree(m_slotIndex);
		rprofFree(m_strings);
	}

	bool init(uint32_t _maxStrings)
	{
		uint32_t capacity = 16;
		while (capacity < _maxStrings * 2)
			capacity <<= 1;

		m_mask		= capacity - 1;
		m_slots		= (const char**)rprofAlloc(sizeof(const char*) * capacity);
		m_slotIndex	= (uint32_t*)rprofAlloc(sizeof(uint32_t) * capacity);
		m_strings	= (const char**)rprofAlloc(sizeof(const char*) * (_maxStrings ? _maxStrings : 1));
		if (!m_slots || !m_slotIndex || !m_strings)
			return false;

		for (uint32_t i=0; i<capacity; ++i)
			m_slots[i] = 0;
		return true;
	}

	// returns the index of the string, adding it if not present
	uint32_t add(const char* _str)
	{
		if (!_str)
			_str = "";

		// fast path: the same pointer was seen before - skip hashing/comparing
		const uint32_t cacheSlot = (uint32_t)(((uintptr_t)_str >> 3) & 255);
		if (m_ptrCache[cacheSlot] == _str)
			return m_ptrCacheIndex[cacheSlot];

		const uint32_t index = find(_str);
		m_ptrCache[cacheSlot]		= _str;
		m_ptrCacheIndex[cacheSlot]	= index;
		return index;
	}

	uint32_t find(const char* _str)
	{
		uint32_t hash = 2166136261u;	// FNV-1a
		for (const char* c = _str; *c; ++c)
			hash = (hash ^ (uint8_t)*c) * 16777619u;

		uint32_t slot = hash & m_mask;
		while (m_slots[slot])
		{
			if ((m_slots[slot] == _str) || (rprofStrCmp(m_slots[slot], _str) == 0))
				return m_slotIndex[slot];
			slot = (slot + 1) & m_mask;
		}

		m_slots[slot]		= _str;
		m_slotIndex[slot]	= m_count;
		m_strings[m_count]	= _str;
		m_totalSize		   += 4 + (uint32_t)rprofStrLen(_str);	// see writeStr for details
		return m_count++;
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
		if (a.m_end      != b.m_end)		return a.m_end      > b.m_end;
		return a.m_level < b.m_level;
	}
};

// groups scopes with identical names next to each other; names of a loaded
// frame are deduplicated, so equal names share a pointer
struct SortName
{
	bool operator()(const ProfilerScope& a, const ProfilerScope& b) const
	{
		return (uintptr_t)a.m_name < (uintptr_t)b.m_name;
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
		// route through the host allocator (placement new) instead of global
		// operator new, so the runtime never touches the CRT heap
		void* mem = rprofAlloc(sizeof(rprof::ProfilerContext));
		g_context = mem ? new (mem) rprof::ProfilerContext() : 0;
		// pre-calibrate the clock frequency now, outside of any lock, so the
		// first rprofBeginFrame() does not stall while holding the mutex
		rprofGetClockFrequency();
	}

	void rprofShutDown()
	{
		if (g_context)
		{
			g_context->~ProfilerContext();
			rprofFree(g_context);
			g_context = 0;
		}
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
		const uint32_t numScopes	= _data->m_numScopes;
		const uint32_t numThreads	= _data->m_numThreads;
		const uint32_t maxStrings	= numScopes * 2 + numThreads;

		// fill string data, remembering each string's index so it is looked up
		// only once
		StringTable strTable;
		uint32_t* strIndices = (uint32_t*)rprofAlloc(sizeof(uint32_t) * (maxStrings ? maxStrings : 1));
		if (!strIndices || !strTable.init(maxStrings))
		{
			rprofFree(strIndices);
			return 0;
		}

		for (uint32_t i=0; i<numScopes; ++i)
		{
			ProfilerScope& scope = _data->m_scopes[i];
			strIndices[i*2 + 0] = strTable.add(scope.m_name);
			strIndices[i*2 + 1] = strTable.add(scope.m_file);
		}
		for (uint32_t i=0; i<numThreads; ++i)
			strIndices[numScopes*2 + i] = strTable.add(_data->m_threads[i].m_name);

		// calc data size, overestimate to be safe
		uint32_t maxTotalSize =	numScopes  * sizeof(ProfilerScope)  +
								numThreads * sizeof(ProfilerThread) +
								sizeof(ProfilerFrame) +
								strTable.m_totalSize;

		uint8_t* buffer = (uint8_t*)rprofAlloc(maxTotalSize);
		if (!buffer)
		{
			rprofFree(strIndices);
			return 0;
		}
		uint8_t* bufPtr = buffer;

		writeVar(buffer, _data->m_startTime);
		writeVar(buffer, _data->m_endtime);
		writeVar(buffer, _data->m_prevFrameTime);
		writeVar(buffer, _data->m_platformID);
		writeVar(buffer, rprofGetClockFrequency());

		// write scopes
		writeVar(buffer, numScopes);
		for (uint32_t i=0; i<numScopes; ++i)
		{
			ProfilerScope& scope = _data->m_scopes[i];
			writeVar(buffer, scope.m_start);
			writeVar(buffer, scope.m_end);
			writeVar(buffer, scope.m_threadID);
			writeVar(buffer, strIndices[i*2 + 0]);
			writeVar(buffer, strIndices[i*2 + 1]);
			writeVar(buffer, scope.m_line);
			writeVar(buffer, scope.m_level);
		}

		// write thread info
		writeVar(buffer, numThreads);
		for (uint32_t i=0; i<numThreads; ++i)
		{
			writeVar(buffer, _data->m_threads[i].m_threadID);
			writeVar(buffer, strIndices[numScopes*2 + i]);
		}

		// write string data
		writeVar(buffer, strTable.m_count);
		for (uint32_t i=0; i<strTable.m_count; ++i)
			writeStr(buffer, strTable.m_strings[i]);

		int compSize = LZ4_compress_default((const char*)bufPtr, (char*)_buffer, (int)(buffer - bufPtr), (int)_bufferSize);
		rprofFree(bufPtr);
		rprofFree(strIndices);
		return compSize;
	}

	void rprofLoad(ProfilerFrame* _data, void* _buffer, size_t _bufferSize)
	{
		// captures typically compress 3-6x; start big enough that the first
		// attempt usually succeeds instead of decompressing twice
		size_t		bufferSize	= _bufferSize * 4;
		uint8_t*	buffer		= 0;
		uint8_t*	bufferPtr;

		// an empty input would never grow the buffer below (0 * 2 == 0)
		if (bufferSize < 64)
			bufferSize = 64;

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

		// pass 1: validate the whole buffer and measure it, so the frame can be
		// built in a single allocation without any partial state to undo
		uint32_t numScopes	= 0;
		uint32_t numThreads	= 0;
		uint32_t numStrings	= 0;
		size_t	 stringBytes = 0;
		bool ok = true;

		ok = ok && readVarSafe(buffer, bufferEnd, _data->m_startTime);
		ok = ok && readVarSafe(buffer, bufferEnd, _data->m_endtime);
		ok = ok && readVarSafe(buffer, bufferEnd, _data->m_prevFrameTime);
		ok = ok && readVarSafe(buffer, bufferEnd, _data->m_platformID);
		ok = ok && readVarSafe(buffer, bufferEnd, _data->m_CPUFrequency);

		// a serialized scope is 40 bytes; checking the count against the data
		// left also rules out integer overflow / over-allocation
		ok = ok && readVarSafe(buffer, bufferEnd, numScopes);
		ok = ok && (numScopes <= (size_t)(bufferEnd - buffer) / 40);
		uint8_t* scopesData = buffer;
		if (ok)
			buffer += (size_t)numScopes * 40;

		// a serialized thread is 12 bytes
		ok = ok && readVarSafe(buffer, bufferEnd, numThreads);
		ok = ok && (numThreads <= (size_t)(bufferEnd - buffer) / 12);
		uint8_t* threadsData = buffer;
		if (ok)
			buffer += (size_t)numThreads * 12;

		// each string is at least a 4-byte length prefix
		ok = ok && readVarSafe(buffer, bufferEnd, numStrings);
		ok = ok && (numStrings <= (size_t)(bufferEnd - buffer) / 4);
		uint8_t* stringsData = buffer;
		for (uint32_t i=0; i<numStrings && ok; ++i)
		{
			uint32_t len = 0;
			ok = readVarSafe(buffer, bufferEnd, len) && ((size_t)(bufferEnd - buffer) >= len);
			if (ok)
			{
				buffer		+= len;
				stringBytes	+= (size_t)len + 1;
			}
		}

		// one block holds scopes + stats scopes, their stats, threads, the
		// string table and the string characters; rprofRelease frees it
		const size_t scopesSize		= sizeof(ProfilerScope)		 * (size_t)numScopes * 2;	// extra space for viewer - m_scopesStats
		const size_t statsSize		= sizeof(ProfilerScopeStats) * (size_t)numScopes * 2;
		const size_t threadsSize	= sizeof(ProfilerThread)	 * (size_t)numThreads;
		const size_t tableSize		= sizeof(const char*)		 * (size_t)numStrings;

		uint8_t* block = ok ? (uint8_t*)rprofAlloc(scopesSize + statsSize + threadsSize + tableSize + stringBytes + 1) : 0;
		if (!block)
		{
			rprofFree(bufferPtr);
			return;
		}

		ProfilerScope*		scopes	= (ProfilerScope*)block;
		ProfilerScopeStats*	stats	= (ProfilerScopeStats*)(block + scopesSize);
		ProfilerThread*		threads	= (ProfilerThread*)(block + scopesSize + statsSize);
		const char**		strings	= (const char**)(block + scopesSize + statsSize + threadsSize);
		char*				chars	= (char*)(block + scopesSize + statsSize + threadsSize + tableSize);

		// pass 2: everything was validated above, parse without bounds checks
		buffer = stringsData;
		for (uint32_t i=0; i<numStrings; ++i)
		{
			uint32_t len;
			readVar(buffer, len);
			memoryCopy(chars, buffer, len);
			chars[len]	= 0;
			strings[i]	= chars;
			chars		+= len + 1;
			buffer		+= len;
		}

		uint32_t strIdx;
		buffer = scopesData;
		for (uint32_t i=0; i<numScopes; ++i)
		{
			ProfilerScope& scope = scopes[i];
			readVar(buffer, scope.m_start);
			readVar(buffer, scope.m_end);
			readVar(buffer, scope.m_threadID);
			readVar(buffer, strIdx);
			scope.m_name = (strIdx < numStrings) ? strings[strIdx] : "";
			readVar(buffer, strIdx);
			scope.m_file = (strIdx < numStrings) ? strings[strIdx] : "";
			readVar(buffer, scope.m_line);
			readVar(buffer, scope.m_level);

			scope.m_stats					= &stats[i];
			scope.m_stats->m_inclusiveTime	= scope.m_end - scope.m_start;
			scope.m_stats->m_exclusiveTime	= scope.m_stats->m_inclusiveTime;
			scope.m_stats->m_occurences		= 0;
		}

		for (uint32_t i=numScopes; i<numScopes*2; ++i)
			scopes[i].m_stats = &stats[i];

		buffer = threadsData;
		for (uint32_t i=0; i<numThreads; ++i)
		{
			readVar(buffer, threads[i].m_threadID);
			readVar(buffer, strIdx);
			threads[i].m_name = (strIdx < numStrings) ? strings[strIdx] : "";
		}

		rprofFree(bufferPtr);

		_data->m_numScopes		= numScopes;
		_data->m_numThreads		= numThreads;
		_data->m_scopes			= scopes;
		_data->m_scopesStats	= &scopes[numScopes];
		_data->m_scopeStatsInfo	= stats;
		_data->m_threads		= threads;

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

			// pop scopes that cannot be this scope's parent: ones that ended
			// before it started, or that are not shallower than it (siblings
			// and their descendants)
			while (sp > 0 &&	((_data->m_scopes[s_stack[sp-1]].m_end < s.m_start) ||
								 (_data->m_scopes[s_stack[sp-1]].m_level >= s.m_level)))
				--sp;

			if (sp > 0)
			{
				// a child may start/end on the same clock tick as its parent
				ProfilerScope& parent = _data->m_scopes[s_stack[sp-1]];
				if ((parent.m_level + 1 == s.m_level) &&
					(s.m_start >= parent.m_start) && (s.m_end <= parent.m_end))
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
			while ((j < numScopesLocal) && (_data->m_scopes[j].m_name == scopeI.m_name))
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
		// only the fixed size header is needed - decode just that instead of
		// decompressing (and possibly re-decompressing) the whole frame
		uint8_t header[	sizeof(uint64_t) * 3 +	// start, end, prev frame time
						sizeof(uint32_t)	 +	// platform ID
						sizeof(uint64_t)];		// frequency

		*_time = 0.0f;

		int decomp = LZ4_decompress_safe_partial((const char*)_buffer, (char*)header, (int)_bufferSize, (int)sizeof(header), (int)sizeof(header));
		if (decomp < (int)sizeof(header))
			return;

		uint64_t startTime, endtime, prevFrameTime, frequency;
		uint32_t platformID;

		uint8_t* buffer = header;
		readVar(buffer, startTime);
		readVar(buffer, endtime);
		readVar(buffer, prevFrameTime);	// dummy
		readVar(buffer, platformID);	// dummy
		readVar(buffer, frequency);

		*_time = rprofClock2ms(endtime - startTime, frequency);
	}

	void rprofRelease(ProfilerFrame* _data)
	{
		// rprofLoad builds the whole frame (including strings) in one block
		rprofFree(_data->m_scopes);

		// leave the frame empty so a repeated release is harmless
		_data->m_scopes			= 0;
		_data->m_scopesStats	= 0;
		_data->m_scopeStatsInfo	= 0;
		_data->m_threads		= 0;
		_data->m_numScopes		= 0;
		_data->m_numScopesStats	= 0;
		_data->m_numThreads		= 0;
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
		// clock() measures process CPU time, not wall time - use a monotonic clock
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		int64_t q = (int64_t)now.tv_sec * 1000000000 + now.tv_nsec;
#elif RPROF_PLATFORM_EMSCRIPTEN
		int64_t q = (int64_t)(emscripten_get_now() * 1000.0);
#elif RPROF_PLATFORM_SWITCH
		int64_t q = nn::os::GetSystemTick().GetInt64Value();
#else
		struct timeval now;
		gettimeofday(&now, 0);
		int64_t q = (int64_t)now.tv_sec * 1000000 + now.tv_usec;
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
		return 1000000000;
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

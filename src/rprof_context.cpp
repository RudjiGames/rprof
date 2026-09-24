/*
 * Copyright 2025 Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include "../inc/rprof.h"
#include "rprof_config.h"
#include "rprof_platform.h"
#include "rprof_context.h"

extern "C" uint64_t rprofGetClockFrequency();

namespace rprof {

	// per thread state; a thread_local access is a plain TLS slot load, much
	// cheaper than the pthread / Tls* key API, which cost two gets and two sets
	// per scope
	static thread_local int			t_level			= 0;
	static thread_local uint64_t	t_threadID		= 0;
	static thread_local bool		t_threadIDValid	= false;

	ProfilerContext::ProfilerContext()
		: m_scopesOpen(0)
		, m_displayScopes(0)
		, m_frameStartTime(0)
		, m_frameEndTime(0)
		, m_thresholdCrossed(false)
		, m_timeThreshold(0.0f)
		, m_levelThreshold(0)
		, m_pauseProfiling(false)
	{
		m_numThreadNames = 0;
		m_scopesDisplay		= m_scopesDisplayBuffers[0];
		m_scopesDisplayBack	= m_scopesDisplayBuffers[1];
		rprofFreeListCreate(sizeof(ProfilerScope), RPROF_SCOPES_MAX, &m_scopesAllocator);

		for (int i=0; i<BufferUse::Count; ++i)
		{
			m_namesSize[i] = 0;
			m_namesData[i] = m_namesDataBuffers[i];
		}
	}

	ProfilerContext::~ProfilerContext()
	{
		rprofFreeListDestroy(&m_scopesAllocator);
	}

	void ProfilerContext::setThreshold(float _ms, int _levelThreshold)
	{
		m_timeThreshold		= _ms;
		m_levelThreshold	= _levelThreshold;
	}

	bool ProfilerContext::isPaused()
	{
		return m_pauseProfiling;
	}

	bool ProfilerContext::wasThresholdCrossed()
	{
		return !m_pauseProfiling && m_thresholdCrossed;
	}

	void ProfilerContext::setPaused(bool _paused)
	{
		m_pauseProfiling = _paused;
	}

	void ProfilerContext::registerThread(uint64_t _threadID, const char* _name)
	{
		ScopedMutexLocker lock(m_mutex);

		// find an existing entry for this thread, or append a new one
		ThreadInfo* slot = 0;
		for (uint32_t i=0; i<m_numThreadNames; ++i)
			if (m_threadNames[i].m_threadID == _threadID)
			{
				slot = &m_threadNames[i];
				break;
			}

		if (!slot)
		{
			if (m_numThreadNames == RPROF_DRAW_THREADS_MAX)
				return;
			slot = &m_threadNames[m_numThreadNames++];
			slot->m_threadID = _threadID;
		}

		// copy the name inline, truncating to fit (always null terminated)
		uint32_t n = 0;
		if (_name)
			while (_name[n] && (n < RPROF_THREAD_NAME_MAX - 1))
			{
				slot->m_name[n] = _name[n];
				++n;
			}
		slot->m_name[n] = 0;
	}

	void ProfilerContext::unregisterThread(uint64_t _threadID)
	{
		ScopedMutexLocker lock(m_mutex);

		for (uint32_t i=0; i<m_numThreadNames; ++i)
			if (m_threadNames[i].m_threadID == _threadID)
			{
				// swap the last entry into this slot to keep the array packed
				m_threadNames[i] = m_threadNames[--m_numThreadNames];
				return;
			}
	}

	void ProfilerContext::beginFrame()
	{
		ScopedMutexLocker lock(m_mutex);

		uint64_t frameBeginTime, frameEndTime;
		static uint64_t beginPrevFrameTime = rprofGetClock();
		frameBeginTime		= beginPrevFrameTime;
		frameEndTime		= rprofGetClock();
		beginPrevFrameTime	= frameEndTime;

		m_thresholdCrossed = false;

		// cache once - the value never changes after first calibration, but it
		// is otherwise re-fetched for every open scope below
		const uint64_t clockFrequency = rprofGetClockFrequency();

		int level = (int)m_levelThreshold - 1;

		uint32_t scopesToRestart = 0;

		m_namesSize[BufferUse::Open] = 0;

		// a paused profiler never publishes this frame, so skip the copies
		const bool fillDisplay = !m_pauseProfiling;
		ProfilerScope* scopesDisplay = m_scopesDisplayBack;

		for (uint32_t i=0; i<m_scopesOpen; ++i)
		{
			ProfilerScope* scope = m_scopesCapture[i];

			// m_end may be written concurrently by endScope on another thread,
			// so read it exactly once, atomically
			uint64_t scopeEnd = rprofAtomicLoad64(&scope->m_end);
			bool stillOpen = (scope->m_start == scopeEnd);

			// the display copy keeps pointing at the name in the capture buffer,
			// which becomes the display buffer below; the Open buffer is only a
			// staging area for restarted scopes and is overwritten every frame
			// copy field by field: a struct copy would read m_end non-atomically
			if (fillDisplay)
			{
				ProfilerScope& display = scopesDisplay[i];
				display.m_start		= scope->m_start;
				display.m_end		= scopeEnd;
				display.m_threadID	= scope->m_threadID;
				display.m_name		= scope->m_name;
				display.m_file		= scope->m_file;
				display.m_line		= scope->m_line;
				display.m_level		= scope->m_level;
				display.m_stats		= 0;
			}

			// did scope cross threshold?
			if (level == (int)scope->m_level)
			{
				uint64_t scopeEndTime = stillOpen ? frameEndTime : scopeEnd;

				if (m_timeThreshold <= rprofClock2ms(scopeEndTime - scope->m_start, clockFrequency))
					m_thresholdCrossed = true;
			}

			// scope that was not closed, spans frame boundary
			// keep it for next frame
			if (stillOpen)
			{
				scope->m_name = addString(scope->m_name, BufferUse::Open);
				m_scopesCapture[scopesToRestart++] = scope;
			}
			else
				rprofFreeListFree(&m_scopesAllocator, scope);
		}

		// did frame cross threshold ?
		float prevFrameTime = rprofClock2ms(frameEndTime - frameBeginTime, clockFrequency);
		if ((level == -1) && (m_timeThreshold <= prevFrameTime))
			m_thresholdCrossed = true;

		if (m_thresholdCrossed && !m_pauseProfiling)
		{
			char* tmpNames = m_namesData[BufferUse::Capture];
			m_namesData[BufferUse::Capture] = m_namesData[BufferUse::Display];
			m_namesData[BufferUse::Display] = tmpNames;

			ProfilerScope* tmpScopes = m_scopesDisplay;
			m_scopesDisplay		= m_scopesDisplayBack;
			m_scopesDisplayBack	= tmpScopes;

			m_displayScopes		= m_scopesOpen;
			m_frameStartTime	= frameBeginTime;
			m_frameEndTime		= frameEndTime;
		}

		m_namesSize[BufferUse::Capture] = 0;
		for (uint32_t i=0; i<scopesToRestart; ++i)
			m_scopesCapture[i]->m_name = addString(m_scopesCapture[i]->m_name, BufferUse::Capture);

		m_scopesOpen	= scopesToRestart;
	}

	int ProfilerContext::incLevel()
	{
		return t_level++;
	}

	void ProfilerContext::decLevel()
	{
		--t_level;
	}

	uint64_t ProfilerContext::getThreadIDCached()
	{
		// getThreadID() can be a real syscall (e.g. gettid on Linux); the id is
		// constant per thread, so fetch it once
		if (!t_threadIDValid)
		{
			t_threadID		= getThreadID();
			t_threadIDValid	= true;
		}
		return t_threadID;
	}

	ProfilerScope* ProfilerContext::beginScope(const char* _file, int _line, const char* _name)
	{
		// thread local and clock queries don't touch shared state - do them
		// before taking the lock to keep the critical section short
		const uint64_t	threadID	= getThreadIDCached();
		const int		level		= incLevel();
		const uint64_t	start		= rprofGetClock();

		ProfilerScope* scope = 0;
		{
			ScopedMutexLocker lock(m_mutex);
			if (m_scopesOpen == RPROF_SCOPES_MAX)
			{
				// endScope(0) is a no-op, so undo the level change here
				decLevel();
				return 0;
			}

			scope = (ProfilerScope*)rprofFreeListAlloc(&m_scopesAllocator);
			m_scopesCapture[m_scopesOpen++] = scope;

			scope->m_name		= addString(_name, BufferUse::Capture);
			scope->m_start		= start;
			rprofAtomicStore64(&scope->m_end, start);
			scope->m_threadID	= threadID;
			scope->m_file		= _file;
			scope->m_line		= _line;
			scope->m_level		= level;
		}

		return scope;
	}

	void ProfilerContext::endScope(ProfilerScope* _scope)
	{
		if (!_scope)
			return;

		// m_end is read by beginFrame (under the mutex) on another thread,
		// while this write happens without the lock - store it atomically
		// to avoid a torn read / data race
		// m_end == m_start marks a scope as still open, so a scope that ends on
		// the same clock tick it started (common with microsecond clocks) must
		// not keep that value, otherwise it is never released
		uint64_t endTime = rprofGetClock();
		if (endTime == _scope->m_start)
			++endTime;
		rprofAtomicStore64(&_scope->m_end, endTime);
		decLevel();
	}

	const char* ProfilerContext::addString(const char* _name, BufferUse _buffer)
	{
		char*	nameData = m_namesData[_buffer];
		int&	nameSize = m_namesSize[_buffer];

		// names buffer is full; return a safe empty string instead of a
		// pointer past the end of the buffer
		if (nameSize >= RPROF_TEXT_MAX)
			return "";

		// copy with a single bound check per character, truncating so the
		// terminator always fits
		char*		ret		= &nameData[nameSize];
		const int	maxLen	= RPROF_TEXT_MAX - nameSize - 1;
		int			len		= 0;
		while ((len < maxLen) && _name[len])
		{
			ret[len] = _name[len];
			++len;
		}
		ret[len] = 0;
		nameSize += len + 1;

		return ret;
	}

	void ProfilerContext::getFrameData(ProfilerFrame* _data)
	{
		ScopedMutexLocker lock(m_mutex);

		static ProfilerThread threadData[RPROF_DRAW_THREADS_MAX];

		uint32_t numThreads = m_numThreadNames;
		if (numThreads > RPROF_DRAW_THREADS_MAX)
			numThreads = RPROF_DRAW_THREADS_MAX;

		_data->m_numScopes		= m_displayScopes;
		_data->m_scopes			= m_scopesDisplay;
		_data->m_numThreads		= numThreads;
		_data->m_threads		= threadData;
		_data->m_startTime		= m_frameStartTime;
		_data->m_endtime		= m_frameEndTime;
		_data->m_prevFrameTime	= m_frameEndTime - m_frameStartTime;
		_data->m_CPUFrequency	= rprofGetClockFrequency();
		_data->m_timeThreshold	= m_timeThreshold;
		_data->m_levelThreshold	= m_levelThreshold;
		_data->m_platformID		= getPlatformID();

		for (uint32_t i=0; i<numThreads; ++i)
		{
			threadData[i].m_threadID	= m_threadNames[i].m_threadID;
			threadData[i].m_name		= m_threadNames[i].m_name;
		}
	}

} // namespace rprof

/*
 * Copyright 2025 Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include "../inc/rprof.h"
#include "rprof_config.h"
#include "rprof_platform.h"
#include "rprof_context.h"
#include "rprof_tls.h"

extern "C" uint64_t rprofGetClockFrequency();

namespace rprof {

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
		m_tlsLevel = tlsAllocate();
		m_tlsThreadID = tlsAllocate();
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
		tlsFree(m_tlsLevel);
		tlsFree(m_tlsThreadID);
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
		m_threadNames[_threadID] = _name;
	}

	void ProfilerContext::unregisterThread(uint64_t _threadID)
	{
		ScopedMutexLocker lock(m_mutex);
		m_threadNames.erase(_threadID);
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

		static ProfilerScope scopesDisplay[RPROF_SCOPES_MAX];
		for (uint32_t i=0; i<m_scopesOpen; ++i)
		{
			ProfilerScope* scope = m_scopesCapture[i];

			// m_end may be written concurrently by endScope on another thread,
			// so read it exactly once, atomically
			uint64_t scopeEnd = rprofAtomicLoad64(&scope->m_end);
			bool stillOpen = (scope->m_start == scopeEnd);

			if (stillOpen)
				scope->m_name = addString(scope->m_name, BufferUse::Open);

			scopesDisplay[i] = *scope;
			scopesDisplay[i].m_end = scopeEnd;

			// scope that was not closed, spans frame boundary
			// keep it for next frame
			if (stillOpen)
				m_scopesCapture[scopesToRestart++] = scope;
			else
			{
				rprofFreeListFree(&m_scopesAllocator, scope);
				scope = &scopesDisplay[i];
			}

			// did scope cross threshold?
			if (level == (int)scope->m_level)
			{
				uint64_t scopeEndTime = stillOpen ? frameEndTime : scopeEnd;

				if (m_timeThreshold <= rprofClock2ms(scopeEndTime - scope->m_start, clockFrequency))
					m_thresholdCrossed = true;
			}
		}

		// did frame cross threshold ?
		float prevFrameTime = rprofClock2ms(frameEndTime - frameBeginTime, clockFrequency);
		if ((level == -1) && (m_timeThreshold <= prevFrameTime))
			m_thresholdCrossed = true;

		if (m_thresholdCrossed && !m_pauseProfiling)
		{
			std::swap(m_namesData[BufferUse::Capture], m_namesData[BufferUse::Display]);

			for (uint32_t i=0; i<m_scopesOpen; ++i)
				m_scopesDisplay[i] = scopesDisplay[i];

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
		// may be a first call on this thread
		void* tl = tlsGetValue(m_tlsLevel);
		if (!tl)
		{
			// we'd like to start with -1 but then the ++ operator below
			// would result in NULL value for tls so we offset by 2
			tl = (void*)1;
			tlsSetValue(m_tlsLevel, tl);
		}
		intptr_t threadLevel = (intptr_t)tl - 1;
		tlsSetValue(m_tlsLevel, (void*)(threadLevel + 2));
		return (int)threadLevel;
	}

	void ProfilerContext::decLevel()
	{
		intptr_t threadLevel = (intptr_t)tlsGetValue(m_tlsLevel);
		--threadLevel;
		tlsSetValue(m_tlsLevel, (void*)threadLevel);
	}

	uint64_t ProfilerContext::getThreadIDCached()
	{
		// getThreadID() can be a real syscall (e.g. gettid on Linux); the id is
		// constant per thread, so fetch it once and cache it in TLS. Store
		// id + 1 so a never-set slot (NULL) is unambiguous, mirroring incLevel.
		void* tl = tlsGetValue(m_tlsThreadID);
		if (!tl)
		{
			uint64_t id = getThreadID();
			tlsSetValue(m_tlsThreadID, (void*)(uintptr_t)(id + 1));
			return id;
		}
		return (uint64_t)((uintptr_t)tl - 1);
	}

	ProfilerScope* ProfilerContext::beginScope(const char* _file, int _line, const char* _name)
	{
		ProfilerScope* scope = 0;
		{
			ScopedMutexLocker lock(m_mutex);
			if (m_scopesOpen == RPROF_SCOPES_MAX)
				return 0;

			scope = (ProfilerScope*)rprofFreeListAlloc(&m_scopesAllocator);
			m_scopesCapture[m_scopesOpen++] = scope;

			scope->m_name		= addString(_name, BufferUse::Capture);
			scope->m_start		= rprofGetClock();
			rprofAtomicStore64(&scope->m_end, scope->m_start);
			scope->m_threadID	= getThreadIDCached();
			scope->m_file		= _file;
			scope->m_line		= _line;
			scope->m_level		= incLevel();
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
		rprofAtomicStore64(&_scope->m_end, rprofGetClock());
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

		char *ret = &nameData[nameSize];
		while (nameSize < RPROF_TEXT_MAX)
		{
			char c = *_name++;
			if (c)
				nameData[nameSize++] = c;
			else
				break;
		}

		if (nameSize < RPROF_TEXT_MAX)
			nameData[nameSize++] = 0;
		else
			nameData[RPROF_TEXT_MAX - 1] = 0;

		return ret;
	}

	void ProfilerContext::getFrameData(ProfilerFrame* _data)
	{
		ScopedMutexLocker lock(m_mutex);

		static ProfilerThread threadData[RPROF_DRAW_THREADS_MAX];

		uint32_t numThreads = (uint32_t)m_threadNames.size();
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

		std::unordered_map<uint64_t, std::string>::iterator it = m_threadNames.begin();
		for (uint32_t i=0; i<numThreads; ++i)
		{
			threadData[i].m_threadID	= it->first;
			threadData[i].m_name		= it->second.c_str();
			++it;
		}
	}

} // namespace rprof

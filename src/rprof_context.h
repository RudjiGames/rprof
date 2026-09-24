/*
 * Copyright 2025 Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#ifndef RPROF_LIB_H
#define RPROF_LIB_H

#include "../inc/rprof.h"
#include "rprof_config.h"
#include "rprof_mutex.h"
#include "rprof_freelist.h"

namespace rprof {

	// fixed-size thread name storage; the runtime never allocates, so thread
	// names live inline instead of in a std::unordered_map<.., std::string>
	struct ThreadInfo
	{
		uint64_t	m_threadID;
		char		m_name[RPROF_THREAD_NAME_MAX];
	};

	class ProfilerContext
	{
		enum BufferUse
		{
			Capture,
			Display,
			Open,

			Count
		};


		Mutex			m_mutex;
		rprofFreeList_t	m_scopesAllocator;
		uint32_t		m_scopesOpen;
		ProfilerScope*	m_scopesCapture[RPROF_SCOPES_MAX];
		// double buffered display scopes: beginFrame fills the back buffer and
		// publishes it by swapping pointers instead of copying every scope
		ProfilerScope	m_scopesDisplayBuffers[2][RPROF_SCOPES_MAX];
		ProfilerScope*	m_scopesDisplay;
		ProfilerScope*	m_scopesDisplayBack;
		uint32_t		m_displayScopes;
		uint64_t		m_frameStartTime;
		uint64_t		m_frameEndTime;
		bool			m_thresholdCrossed;
		float			m_timeThreshold;
		uint32_t		m_levelThreshold;
		bool			m_pauseProfiling;
		char			m_namesDataBuffers[BufferUse::Count][RPROF_TEXT_MAX];
		char*			m_namesData[BufferUse::Count];
		int				m_namesSize[BufferUse::Count];

		ThreadInfo		m_threadNames[RPROF_DRAW_THREADS_MAX];
		uint32_t		m_numThreadNames;

	public:
		// placement construction routed through the host allocator; avoids both
		// the CRT global operator new and any dependency on <new>
		static void* operator new   (size_t, void* _ptr) { return _ptr; }
		static void  operator delete(void*,  void*)      {}

		ProfilerContext();
		~ProfilerContext();

		void			setThreshold(float _ms, int _levelThreshold);
		bool			isPaused();
		bool			wasThresholdCrossed();
		void			setPaused(bool _paused);
		void			registerThread(uint64_t _threadID, const char* _name);
		void			unregisterThread(uint64_t _threadID);
		void			beginFrame();
		int				incLevel();
		void			decLevel();
		uint64_t		getThreadIDCached();
		ProfilerScope*	beginScope(const char* _file, int _line, const char* _name);
		void			endScope(ProfilerScope* _scope);
		const char*		addString(const char* _name, BufferUse _buffer);
		void			getFrameData(ProfilerFrame* _data);
	};

} // namespace rprof

#endif // RPROF_LIB_H

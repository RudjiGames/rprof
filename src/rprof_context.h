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

#include <unordered_map>
#include <string>

namespace rprof {

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
		ProfilerScope	m_scopesDisplay[RPROF_SCOPES_MAX];
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
		uint32_t		m_tlsLevel;

		std::unordered_map<uint64_t, std::string>	m_threadNames;

	public:
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
		ProfilerScope*	beginScope(const char* _file, int _line, const char* _name);
		void			endScope(ProfilerScope* _scope);
		const char*		addString(const char* _name, BufferUse _buffer);
		void			getFrameData(ProfilerFrame* _data);
	};

} // namespace rprof

#endif // RPROF_LIB_H

/*
 * Copyright (c) 2020 by Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#ifndef RPROF_DRAW_H
#define RPROF_DRAW_H

#include "rprof.h"
#include <algorithm>
#include <inttypes.h>

	#define RPROF_DESIRED_FRAME_RATE	 30.0f
	#define RPROF_MINIMUM_FRAME_RATE	 20.0f
	#define RPROF_FLASHL_TIME_IN_MS		333.0f

	static const int	s_maxLevelColors = 11;
	static const ImU32	s_levelColors[s_maxLevelColors] = {
		IM_COL32( 90, 150, 110, 255),	IM_COL32( 80, 180, 115, 255),
		IM_COL32(129, 195, 110, 255),	IM_COL32(170, 190, 100, 255),
		IM_COL32(210, 200,  80, 255),	IM_COL32(230, 210, 115, 255),
		IM_COL32(240, 180,  90, 255),	IM_COL32(240, 140,  65, 255),
		IM_COL32(250, 110,  40, 255),	IM_COL32(250,  75,  25, 255),
		IM_COL32(250,  50,   0, 255)
	};

	static uint64_t		s_timeSinceStatClicked	= rprofGetClock();
	static const char*	s_statClickedName		= 0;
	static uint32_t		s_statClickedLevel		= 0;

	struct PanAndZoon
	{
		float	m_offset;
		float	m_startPan;
		float	m_zoom;

		PanAndZoon()
		{
			m_offset	= 0.0f;
			m_startPan	= 0.0f;
			m_zoom		= 1.0f;
		}

		inline float w2s(float wld, float minX, float maxX) {
			return minX + wld * (maxX - minX) * m_zoom - m_offset;
		}
		inline float w2sdelta(float wld, float minX, float maxX) {
			return wld * (maxX - minX) * m_zoom;
		}
		inline float s2w(float scr, float minX, float maxX) {
			return (scr + m_offset - minX) / ((maxX - minX) * m_zoom);
		}
	};

	template <typename T>
	static inline T rprofMax(T _v1, T _v2) { return _v1 > _v2 ? _v1 : _v2; }
	template <typename T>
	static inline T rprofMin(T _v1, T _v2) { return _v1 < _v2 ? _v1 : _v2; }

	static inline void flashColor(ImU32& _drawColor, uint64_t _elapsedTime)
	{
		ImVec4 white4 = ImColor(IM_COL32_WHITE);

		float msSince = rprofClock2ms(_elapsedTime, rprofGetClockFrequency());
		msSince = rprofMin(msSince, RPROF_FLASHL_TIME_IN_MS);
		msSince = 1.0f - (msSince / RPROF_FLASHL_TIME_IN_MS);

		ImVec4 col4 = ImColor(_drawColor);
		_drawColor = ImColor(	col4.x + (white4.x - col4.x) * msSince,
								col4.y + (white4.y - col4.y) * msSince,
								col4.z + (white4.z - col4.z) * msSince,
								255.0f);
	}

	static inline void flashColorNamed(ImU32& _drawColor, ProfilerScope& _cs, uint64_t _elapsedTime)
	{
		if (s_statClickedName && (strcmp(_cs.m_name, s_statClickedName) == 0) && (_cs.m_level == s_statClickedLevel))
			flashColor(_drawColor, _elapsedTime);
	}

	static inline ImVec4 triColor(float _cmp, float _min1, float _min2)
	{
		ImVec4 col = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
		if (_cmp > _min1) col = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
		if (_cmp > _min2) col = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
		return col;
	}

	struct SortScopes
	{
		bool operator()(const ProfilerScope& a, const ProfilerScope& b) const
		{
			if (a.m_threadID < b.m_threadID)	return true;
			if (b.m_threadID < a.m_threadID)	return false;

			if (a.m_level < b.m_level) return true;
			if (b.m_level < a.m_level) return false;

			if (a.m_start < b.m_start) return true;
			if (b.m_start < a.m_start) return false;

			return false;
		}
	};

	/* Draws a frame capture inspector dialog using ImGui. */
	/* _data       - [in/out] profiler data / single frame capture. User is responsible to release memory using rprofRelease */
	/* _buffer     - buffer to store data to */
	/* _bufferSize - maximum size of buffer, in bytes */
	/* Returns: if frame was saved, returns number of bytes written - see rprofSave. */
	static inline int rprofDrawFrame(ProfilerFrame* _data, void* _buffer = 0, size_t _bufferSize = 0, bool _inGame = true, bool _multi = false)
	{
		int ret = 0;

		SortScopes customLess;
		std::sort(&_data->m_scopes[0], &_data->m_scopes[_data->m_numScopes], customLess);

		ImGui::SetNextWindowPos(ImVec2(10.0f, _multi ? 160.0f : 10.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(900.0f, 480.0f), ImGuiCond_FirstUseEver);

		static bool pause = false;
		bool noMove = (pause && _inGame) || !_inGame;
		noMove =  noMove && ImGui::GetIO().KeyCtrl;

		static ImVec2 winpos = ImGui::GetWindowPos();

		if (noMove)
			ImGui::SetNextWindowPos(winpos);

		ImGui::Begin("Frame inspector", 0, noMove ? ImGuiWindowFlags_NoMove : 0);

		if (!noMove)
			winpos = ImGui::GetWindowPos();

		float deltaTime = rprofClock2ms(_data->m_endtime - _data->m_startTime, _data->m_CPUFrequency);
		float frameRate = 1000.0f / deltaTime;

		ImVec4 col = triColor(frameRate, RPROF_MINIMUM_FRAME_RATE, RPROF_DESIRED_FRAME_RATE);
		 
		ImGui::Text("FPS: ");
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, col);
		ImGui::Text("%.1f    ", 1000.0f / deltaTime);
		ImGui::PopStyleColor();
		ImGui::SameLine();
		ImGui::Text("Frame time: ");
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, col);
		ImGui::Text("%.3f ms   ", deltaTime);
		ImGui::PopStyleColor();
		ImGui::SameLine();

		if (_inGame)
		{
			ImGui::Text("Average FPS: ");
			ImGui::PushStyleColor(ImGuiCol_Text, triColor(ImGui::GetIO().Framerate, RPROF_MINIMUM_FRAME_RATE, RPROF_DESIRED_FRAME_RATE));
			ImGui::SameLine();
			ImGui::Text("%.1f   ", ImGui::GetIO().Framerate);
			ImGui::PopStyleColor();
		}
		else
		{
			float prevFrameTime = rprofClock2ms(_data->m_prevFrameTime, _data->m_CPUFrequency);
			ImGui::SameLine();
			ImGui::Text("Prev frame: ");
			ImGui::PushStyleColor(ImGuiCol_Text, triColor(1000.0f/prevFrameTime, RPROF_MINIMUM_FRAME_RATE, RPROF_DESIRED_FRAME_RATE));
			ImGui::SameLine();
			ImGui::Text("%.3f ms  %.2f fps   ", prevFrameTime, 1000.0f/prevFrameTime);
			ImGui::PopStyleColor();
			ImGui::SameLine();

			ImGui::Text("Platform: ");
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 1.0f, 1.0f, 1.0f));
			ImGui::SameLine();
			ImGui::Text("%s   ", rprofGetPlatformName(_data->m_platformID));
			ImGui::PopStyleColor();
		}

		if (_inGame)
		{
			ImGui::SameLine();
			ImGui::Checkbox("Pause captures   ", &pause);
		}

		bool resetZoom				= false;
		static float threshold		= 0.0f;
		static int thresholdLevel	= 0;

		if (_inGame)
		{
			ImGui::PushItemWidth(210);
			ImGui::SliderFloat("Threshold   ", &threshold, 0.0f, 15.0f);

			ImGui::SameLine();
			ImGui::PushItemWidth(120);
			ImGui::SliderInt("Level", &thresholdLevel, 0, 23);

			ImGui::SameLine();
			if (ImGui::Button("Save frame"))
				ret = rprofSave(_data, _buffer, _bufferSize);
		}
		else
		{
			ImGui::Text("Capture threshold: ");
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0, 1.0f, 1.0f, 1.0f), "%.2f ", _data->m_timeThreshold);
			ImGui::SameLine();
			ImGui::Text("ms   ");
			ImGui::SameLine();
			ImGui::Text("Threshold level: ");
			ImGui::SameLine();
			if (_data->m_levelThreshold == 0)
				ImGui::TextColored(ImVec4(0, 1.0f, 1.0f, 1.0f), "whole frame   ");
			else
				ImGui::TextColored(ImVec4(0, 1.0f, 1.0f, 1.0f), "%d   ", _data->m_levelThreshold);
		}

		ImGui::SameLine();
		resetZoom = ImGui::Button("Reset zoom and pan");

		const ImVec2 p = ImGui::GetCursorScreenPos();
		const ImVec2 s = ImGui::GetWindowSize();

		float frameStartX	= p.x + 3.0f;
		float frameEndX		= frameStartX + s.x - 23;
		float frameStartY	= p.y;

		static PanAndZoon paz;

		ImVec2 mpos = ImGui::GetMousePos();

		if (ImGui::IsMouseDragging(0) && ImGui::GetIO().KeyCtrl)
		{
			paz.m_offset  -= (mpos.x - paz.m_startPan);
			paz.m_startPan = mpos.x;
		}
		else
			paz.m_startPan = mpos.x;

		float mXpre = paz.s2w(mpos.x, frameStartX, frameEndX);

		if (ImGui::GetIO().KeyCtrl)
			paz.m_zoom += ImGui::GetIO().MouseWheel / 30.0f;
		if (ImGui::IsKeyPressed(ImGuiKey_A) && ImGui::IsWindowHovered())
			paz.m_zoom *= 1.1f;
		if (ImGui::IsKeyPressed(ImGuiKey_Z) && ImGui::IsWindowHovered())
			paz.m_zoom /= 1.1f;

		paz.m_zoom  = rprofMax(paz.m_zoom, 1.0f);

		float mXpost  = paz.s2w(mpos.x, frameStartX, frameEndX);
		float mXdelta = mXpost - mXpre;

		paz.m_offset -= paz.w2sdelta(mXdelta, frameStartX, frameEndX);

		// snap to edge
		float leX	= paz.w2s(0.0f, frameStartX, frameEndX);
		float reX	= paz.w2s(1.0f, frameStartX, frameEndX);

		if (leX > frameStartX)
			paz.m_offset += leX - frameStartX;
		if (reX < frameEndX)
			paz.m_offset -= frameEndX - reX;

		if (resetZoom || (_inGame ? !pause : false))
		{
			paz.m_offset = 0.0f;
			paz.m_zoom   = 1.0f;
		}

		rprofSetPaused(pause);
		rprofSetThreshold(threshold, thresholdLevel);

		static const int	RPROF_MAX_FRAME_TIMES = 128;
		static float s_frameTimes[RPROF_MAX_FRAME_TIMES];
		static int	 s_currentFrame = 0;

		float maxFrameTime = 0.0f;
		if (_inGame)
		{
			frameStartY	+= 62.0f;

			if (s_currentFrame == 0)
				memset(s_frameTimes, 0, sizeof(s_frameTimes));

			if (!rprofIsPaused())
			{
				s_frameTimes[s_currentFrame % RPROF_MAX_FRAME_TIMES] = deltaTime;
				++s_currentFrame;
			}

			float frameTimes[RPROF_MAX_FRAME_TIMES];
			for (int i=0; i<RPROF_MAX_FRAME_TIMES; ++i)
			{
				frameTimes[i] = s_frameTimes[(s_currentFrame  + i) % RPROF_MAX_FRAME_TIMES];
				maxFrameTime = rprofMax(maxFrameTime, frameTimes[i]);
			}

			ImGui::Separator();
			ImGui::PlotHistogram("", frameTimes, RPROF_MAX_FRAME_TIMES, 0, "", 0.f, maxFrameTime, ImVec2(s.x - 9.0f, 45));
		}
		else
		{
			frameStartY	+= 12.0f;
			ImGui::Separator();
		}

		ImDrawList* draw_list = ImGui::GetWindowDrawList();

		maxFrameTime = rprofMax(maxFrameTime, 0.001f);
		float pct30fps = 33.33f / maxFrameTime;
		float pct60fps = 16.66f / maxFrameTime;

		float minHistY = p.y + 6.0f;
		float maxHistY = p.y + 45.0f;
		
		float limit30Y = maxHistY - (maxHistY - minHistY) * pct30fps;
		float limit60Y = maxHistY - (maxHistY - minHistY) * pct60fps;

		if (pct30fps <= 1.0f)
		draw_list->AddLine(ImVec2(frameStartX - 3.0f, limit30Y), ImVec2(frameEndX + 3.0f, limit30Y), IM_COL32(255,255, 96,255));

		if (pct60fps <= 1.0f)
		draw_list->AddLine(ImVec2(frameStartX - 3.0f, limit60Y), ImVec2(frameEndX + 3.0f, limit60Y), IM_COL32( 96,255, 96,255));

		if (_data->m_numScopes == 0)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.23f, 0.23f, 1.0f), "No scope data!");
			ImGui::End();
			return ret;
		}

		uint64_t threadID = _data->m_scopes[0].m_threadID;
		bool writeThreadName = true;

		uint64_t totalTime = _data->m_endtime - _data->m_startTime;

		float barHeight = 21.0f;
		float bottom	= 0.0f;

		uint64_t currTime = rprofGetClock();

		for (uint32_t i=0; i<_data->m_numScopes; ++i)
		{
			ProfilerScope& cs = _data->m_scopes[i];
			if (!cs.m_name)
				continue;

			if (cs.m_threadID != threadID)
			{
				threadID		= cs.m_threadID;
				frameStartY		= bottom + barHeight;
				writeThreadName	= true;
			}

			if (writeThreadName)
			{
				ImVec2 tlt = ImVec2(frameStartX,	frameStartY);
				ImVec2 brt = ImVec2(frameEndX,		frameStartY + barHeight);

				draw_list->PushClipRect(tlt, brt, true);
				draw_list->AddRectFilled(tlt, brt, IM_COL32(45, 45, 60, 255));
				const char* threadName = "Unnamed thread";
				for (uint32_t j=0; j<_data->m_numThreads; ++j)
					if (_data->m_threads[j].m_threadID == threadID)
					{
						threadName = _data->m_threads[j].m_name;
						break;
					}
				tlt.x += 3;
				char buffer[512];
				snprintf(buffer, 512, "%s  -  0x%" PRIx64, threadName, threadID);
				draw_list->AddText(tlt, IM_COL32(255, 255, 255, 255), buffer);
				draw_list->PopClipRect();

				frameStartY		+= barHeight;
				writeThreadName	 = false;
			}

			// handle wrap around
			int64_t sX = int64_t(cs.m_start	- _data->m_startTime);
			if (sX < 0) sX = -sX;
			int64_t eX = int64_t(cs.m_end - _data->m_startTime);
			if (eX < 0) eX = -eX;

			float startXpct = float(sX) / float(totalTime);
			float endXpct	= float(eX) / float(totalTime);

			float startX	= paz.w2s(startXpct, frameStartX, frameEndX);
			float endX		= paz.w2s(endXpct  , frameStartX, frameEndX);

			ImVec2 tl = ImVec2(startX,	frameStartY + cs.m_level * (barHeight + 1.0f));
			ImVec2 br = ImVec2(endX,	frameStartY + cs.m_level * (barHeight + 1.0f) + barHeight);
			
			bottom = rprofMax(bottom, br.y);

			int level = cs.m_level;
			if (cs.m_level >= s_maxLevelColors)
				level = s_maxLevelColors - 1;

			ImU32 drawColor = s_levelColors[level];
			flashColorNamed(drawColor, cs, currTime - s_timeSinceStatClicked);

			if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(tl, br) && ImGui::IsWindowHovered())
			{
				s_timeSinceStatClicked	= currTime;
				s_statClickedName		= cs.m_name;
				s_statClickedLevel		= cs.m_level;
			}

			if ((thresholdLevel == (int)cs.m_level + 1) && (threshold <= rprofClock2ms(cs.m_end - cs.m_start, _data->m_CPUFrequency)))
				flashColor(drawColor, currTime - _data->m_endtime);

			draw_list->PushClipRect(tl, br, true);
			draw_list->AddRectFilled(tl, br, drawColor);
			tl.x += 3;
			draw_list->AddText(tl, IM_COL32(0, 0, 0, 255), cs.m_name);
			draw_list->PopClipRect();

			if (ImGui::IsMouseHoveringRect(tl, br) && ImGui::IsWindowHovered())
			{
				ImGui::BeginTooltip();
				ImGui::TextColored(ImVec4(255, 255, 0, 255), "%s", cs.m_name);
				ImGui::Separator();
				ImGui::TextColored(ImVec4(0, 255, 255, 255), "Time: ");
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(230, 230, 230, 255), "%.3f ms", rprofClock2ms(cs.m_end - cs.m_start, _data->m_CPUFrequency));
				ImGui::TextColored(ImVec4(0, 255, 255, 255), "File: ");
				ImGui::SameLine();
				ImGui::Text("%s", cs.m_file);
				ImGui::TextColored(ImVec4(0, 255, 255, 255), "%s", "Line: ");
				ImGui::SameLine();
				ImGui::Text("%d", cs.m_line);
				ImGui::EndTooltip();
			}
		}

		ImGui::End();
		return ret;
	}

	struct sortExcusive {
		bool operator()(const ProfilerScope& a, const ProfilerScope& b) const {
			return  (a.m_stats->m_exclusiveTimeTotal > b.m_stats->m_exclusiveTimeTotal);
		}
	} customLessExc;

	struct sortInclusive {
		bool operator()(const ProfilerScope& a, const ProfilerScope& b) const {
			return  (a.m_stats->m_inclusiveTimeTotal > b.m_stats->m_inclusiveTimeTotal);
		}
	} customLessInc;

	/* Draws a frame capture statistics using ImGui. */
	/* NB: frame data **MUST** be processed (done in rprofLoad) before using this function. */
	/* _data       - [in/out] profiler data / single frame capture. User is responsible to release memory using rprofRelease */
	static inline void rprofDrawStats(ProfilerFrame* _data, bool _multi = false)
	{
		ImGui::SetNextWindowPos(ImVec2(920.0f, _multi ? 160.0f : 10.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(600.0f, 900.0f), ImGuiCond_FirstUseEver);

		ImGui::Begin("Frame stats");

		if (_data->m_numScopes == 0)
		{
			ImGui::TextColored(ImVec4(1.0f,0.23f,0.23f,1.0f), "No scope data!");
			ImGui::End();
			return;
		}

		float deltaTime = rprofClock2ms(_data->m_endtime - _data->m_startTime, _data->m_CPUFrequency);

		static int exclusive = 0;
		ImGui::Text("Sort by:  ");
		ImGui::SameLine();
		ImGui::RadioButton("Exclusive time", &exclusive, 0);
		ImGui::SameLine();
		ImGui::RadioButton("Inclusive time", &exclusive, 1);
		ImGui::Separator();

		if (exclusive == 0)
			std::sort(&_data->m_scopesStats[0], &_data->m_scopesStats[_data->m_numScopesStats], customLessExc);
		else
			std::sort(&_data->m_scopesStats[0], &_data->m_scopesStats[_data->m_numScopesStats], customLessInc);

		const ImVec2 p = ImGui::GetCursorScreenPos();
		const ImVec2 s = ImGui::GetWindowSize();

		float frameStartX	= p.x + 3.0f;
		float frameEndX		= frameStartX + s.x - 23;
		float frameStartY	= p.y + 21.0f;

		uint64_t totalTime = 0;
		if (exclusive == 0)
			totalTime = _data->m_scopesStats[0].m_stats->m_exclusiveTimeTotal;
		else
			totalTime = _data->m_scopesStats[0].m_stats->m_inclusiveTimeTotal;

		float barHeight = 21.0f;

		ImDrawList* draw_list = ImGui::GetWindowDrawList();

		for (uint32_t i=0; i<_data->m_numScopesStats; i++)
		{
			ProfilerScope& cs = _data->m_scopesStats[i];

			float endXpct = float(cs.m_stats->m_exclusiveTimeTotal) / float(totalTime);
			if (exclusive == 1)
				endXpct = float(cs.m_stats->m_inclusiveTimeTotal) / float(totalTime);

			float startX = frameStartX;
			float endX = frameStartX + endXpct * (frameEndX - frameStartX);

			ImVec2 tl = ImVec2(startX, frameStartY);
			ImVec2 br = ImVec2(endX, frameStartY + barHeight);

			int colIdx = s_maxLevelColors - 1 - i;
			if (colIdx < 0) colIdx = 0;
			ImU32 drawColor = s_levelColors[colIdx];

			ImVec2 brE = ImVec2(frameEndX, frameStartY + barHeight);
			bool hoverRow = ImGui::IsMouseHoveringRect(tl, brE);

			if (ImGui::IsMouseClicked(0) && hoverRow && ImGui::IsWindowHovered())
			{
				s_timeSinceStatClicked	= rprofGetClock();
				s_statClickedName		= cs.m_name;
				s_statClickedLevel		= cs.m_level;
			}

			flashColorNamed(drawColor, cs, rprofGetClock() - s_timeSinceStatClicked);

			char buffer[1024];
			snprintf(buffer, 1024, "[%d] %s", cs.m_stats->m_occurences, cs.m_name);
			draw_list->PushClipRect(tl, br, true);
			draw_list->AddRectFilled(tl, br, drawColor);
			draw_list->AddText(tl, IM_COL32(0, 0, 0, 255), buffer);
			draw_list->AddText(ImVec2(startX + 1, frameStartY + 1), IM_COL32(255, 255, 255, 255), buffer);
			draw_list->PopClipRect();

			if (hoverRow && ImGui::IsWindowHovered())
			{
				ImVec2 htl = ImVec2(endX, frameStartY);
				draw_list->PushClipRect(htl, brE, true);
				draw_list->AddRectFilled(htl, brE, IM_COL32(64,64,64,255));
				draw_list->AddText(tl, IM_COL32(0, 0, 0, 255), buffer);
				draw_list->AddText(ImVec2(startX + 1, frameStartY + 1), IM_COL32(192, 192, 192, 255), buffer);
				draw_list->PopClipRect();

				ImGui::BeginTooltip();
				ImGui::TextColored(ImVec4(255, 255, 0, 255), "%s", cs.m_name);
				ImGui::Separator();

				float ttime;
				if (exclusive == 0)
				{
					ImGui::TextColored(ImVec4(0, 255, 255, 255), "Exclusive time total: ");
					ImGui::SameLine();
					ttime = rprofClock2ms(cs.m_stats->m_exclusiveTimeTotal, _data->m_CPUFrequency);
					ImGui::TextColored(ImVec4(230, 230, 230, 255), "%.4f ms", ttime);
				}
				else
				{
					ImGui::TextColored(ImVec4(0, 255, 255, 255), "Inclusive time total: ");
					ImGui::SameLine();
					ttime = rprofClock2ms(cs.m_stats->m_inclusiveTimeTotal, _data->m_CPUFrequency);
					ImGui::TextColored(ImVec4(230, 230, 230, 255), "%.4f ms", ttime);
				}

				ImGui::TextColored(ImVec4(0, 255, 255, 255), "Of frame: ");
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(230, 230, 230, 255), "%2.2f %%", 100.0f*ttime/deltaTime);

				ImGui::TextColored(ImVec4(0, 255, 255, 255), "File: ");
				ImGui::SameLine();
				ImGui::Text("%s", cs.m_file);

				ImGui::TextColored(ImVec4(0, 255, 255, 255), "Line: ");
				ImGui::SameLine();
				ImGui::Text("%d", cs.m_line);

				ImGui::TextColored(ImVec4(0, 255, 255, 255), "Count: ");
				ImGui::SameLine();
				ImGui::Text("%d", cs.m_stats->m_occurences);

				ImGui::EndTooltip();
			}

			frameStartY += 1.0f + barHeight;
		}

		ImGui::End();
	}

#endif // RPROF_DRAW_H

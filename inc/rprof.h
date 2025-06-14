/*
 * Copyright 2025 Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 *
 * rprof - profiling library
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE
 */

#ifndef RPROF_RPROF_H
#define RPROF_RPROF_H

#include <stdint.h> /* uint*_t */
#include <stddef.h> /* size_t  */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*--------------------------------------------------------------------------
 * Structures describing profiling data
 *------------------------------------------------------------------------*/
typedef struct ProfilerScopeStats
{
	uint64_t			m_inclusiveTime;
	uint64_t			m_exclusiveTime;
	uint64_t			m_inclusiveTimeTotal;
	uint64_t			m_exclusiveTimeTotal;
	uint32_t			m_occurences;

} ProfilerScopeStats;

typedef struct ProfilerScope
{
	uint64_t			m_start;
	uint64_t			m_end;
	uint64_t			m_threadID;
	const char*			m_name;
	const char*			m_file;
	uint32_t			m_line;
	uint32_t			m_level;
	ProfilerScopeStats*	m_stats;

} ProfilerScope;

typedef struct ProfilerThread
{
	uint64_t			m_threadID;
	const char*			m_name;

} ProfilerThread;

typedef struct ProfilerFrame
{
	uint32_t			m_numScopes;
	uint32_t			m_numThreads		: 20;
	uint32_t			m_platformID		:  4;
	uint32_t			m_levelThreshold	:  8;
	ProfilerScope*		m_scopes;
	ProfilerThread*		m_threads;
	uint64_t			m_startTime;
	uint64_t			m_endtime;
	uint64_t			m_prevFrameTime;
	uint64_t			m_CPUFrequency;
	float				m_timeThreshold;
	uint32_t			m_numScopesStats;
	ProfilerScope*		m_scopesStats;
	ProfilerScopeStats*	m_scopeStatsInfo;

} ProfilerFrame;

/*--------------------------------------------------------------------------
 * API
 *------------------------------------------------------------------------*/

	/* Initialize profiling library. */
	void rprofInit();

	/* Shut down profiling library and release all resources. */
	void rprofShutDown();

	/* Sets the minimum time (in ms) to trigger a capture call back. */
	/* @param[in] _ms    - time in ms to use as minimum, if set to 0 (defauult) then every frame triggers a call back */
	/* @param[in] _level - scope depth in which to look for scopes longer than _ms threshold, 0 is for entire frame, maximum is 256. */
	void rprofSetThreshold(float _ms, int _level = 0);

	/* Registers thread name. */
	/* @param[in] _name     - name to use for this thread */
	/* @param[in] _threadID - ID of thread to register, 0 for current thread. */
	void rprofRegisterThread(const char* _name, uint64_t _threadID = 0);

	/* Unregisters thread name and releases name string. */
	/* @param[in] _threadID - thread ID */
	void rprofUnregisterThread(uint64_t _threadID);

	/* Must be called once per frame at the frame start */
	void rprofBeginFrame();

	/* Begins a profiling scope/block. */
	/* @param[in] _file - name of source file */
	/* @param[in] _line - line of source file */
	/* @param[in] _name - name of the scope */
	/* @returns scope handle */
	uintptr_t rprofBeginScope(const char* _file, int _line, const char* _name);

	/* Stops a profiling scope/block. */
	/* @param[in] _scopeHandle	- handle of the scope to be closed */
	void rprofEndScope(uintptr_t _scopeHandle);

	/* Returns non zero value if profiling is paused. */
	int rprofIsPaused();

	/* Returns non zero value if the last captured frame has crossed threshold. */
	int rprofWasThresholdCrossed();

	/* Pauses profiling if the value passed is 0, otherwise resumes profiling. */
	/* @param[in] _paused    	- 0 for pause, any other value to resume */
	void rprofSetPaused(int _paused);

	/* Fetches data of the last saved frame (either threshold exceeded or profiling is paused). */
	/* @param[out] _data    	- Pointer to frame data structure */
	void rprofGetFrame(ProfilerFrame* _data);

	/* Saves profiler data to a binary buffer. */
	/* @param[in] _data       - profiler data / single frame capture */
	/* @param[in,out] _buffer - buffer to store data to */
	/* @param[in] _bufferSize - maximum size of buffer, in bytes */
	/* @returns number of bytes written to buffer. 0 for failure. */
	int rprofSave(ProfilerFrame* _data, void* _buffer, size_t _bufferSize);

	/* Loads a single frame capture from a binary buffer. */
	/* @param[in] _data       - [in/out] profiler data / single frame capture. User is responsible to release memory using rprofRelease. */
	/* @param[in,out] _buffer - buffer to store data to */
	/* @param[in] _bufferSize - maximum size of buffer, in bytes */
	void rprofLoad(ProfilerFrame* _data, void* _buffer, size_t _bufferSize);

	/* Loads a only time in miliseconds for a single frame capture from a binary buffer. */
	/* @param[in] _time       - [in/out] frame timne in ms. */
	/* @param[in,out] _buffer - buffer to store data to */
	/* @param[in] _bufferSize - maximum size of buffer, in bytes */
	void rprofLoadTimeOnly(float* _time, void* _buffer, size_t _bufferSize);

	/* Releases resources for a single frame capture. Only valid for data loaded with rprofLoad. */
	/* @param[in] _data       - data to be released */
	void rprofRelease(ProfilerFrame* _data);
	
	/* Returns CPU clock. */
	/* @returns CPU clock counter. */
	uint64_t rprofGetClock();

	/* Returns CPU frequency. */
	/* @returns CPU frequency. */
	uint64_t rprofGetClockFrequency();

	/* Calculates miliseconds from CPU clock. */
	/* @param[in] _clock       - data to be released */
	/* @param[in] _frequency   - data to be released */
	/* @returns time in milliseconds. */
	float rprofClock2ms(uint64_t _clock, uint64_t _frequency);

	/* Returns name of the platform. */
	/* @param[in] _platformID - platform identifier */
	/* @returns platform name as string. */
	const char* rprofGetPlatformName(uint8_t _platformID);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#ifdef __cplusplus

struct rprofScoped
{
	uintptr_t	m_scope;

	rprofScoped(const char* _file, int _line, const char* _name)
	{
		m_scope = rprofBeginScope(_file, _line, _name);
	}

	~rprofScoped()
	{
		rprofEndScope(m_scope);
	}
};

/*--------------------------------------------------------------------------
 * Macro used to profile on a scope basis
 *------------------------------------------------------------------------*/
#ifndef RPROF_DISABLE_PROFILING

#define RPROF_CONCAT2(_x, _y) _x ## _y
#define RPROF_CONCAT(_x, _y) RPROF_CONCAT2(_x, _y)

#define RPROF_INIT()				rprofInit()
#define RPROF_SCOPE(x, ...)			rprofScoped RPROF_CONCAT(profileScope,__LINE__)(__FILE__, __LINE__, x)
#define RPROF_BEGIN_FRAME()			rprofBeginFrame()
#define RPROF_REGISTER_THREAD(n)	rprofRegisterThread(n)
#define RPROF_SHUTDOWN()			rprofShutDown()
#else
#define RPROF_INIT()				void()
#define RPROF_SCOPE(...)			void()
#define RPROF_BEGIN_FRAME()			void()
#define RPROF_REGISTER_THREAD(n)	void()
#define RPROF_SHUTDOWN()			void()
#endif /* RPROF_DISABLE_PROFILING */

#endif /* __cplusplus */

#endif /* RPROF_RPROF_H */

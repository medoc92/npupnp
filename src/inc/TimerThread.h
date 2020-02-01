/*******************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation 
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met: 
 *
 * * Redistributions of source code must retain the above copyright notice, 
 * this list of conditions and the following disclaimer. 
 * * Redistributions in binary form must reproduce the above copyright notice, 
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution. 
 * * Neither name of Intel Corporation nor the names of its contributors 
 * may be used to endorse or promote products derived from this software 
 * without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR 
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY 
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************/

#ifndef TIMERTHREAD_H
#define TIMERTHREAD_H

#include <list>

#include "FreeList.h"
#include "ithread.h"
#include "ThreadPool.h"

#define INVALID_EVENT_ID (-10 & 1<<29)

/*! Timeout Types. */
typedef enum timeoutType {
	/*! seconds from Jan 1, 1970. */
	ABS_SEC,
	/*! seconds from current time. */
	REL_SEC
} TimeoutType;

/*!
 * Struct to contain information for a timer event.
 *
 * Internal to the TimerThread, needs to be defined here because of
   the FreeList<TimerEvent> below.
 */
struct TimerEvent {
	ThreadPoolJob job;
	/*! [in] Absolute time for event in seconds since Jan 1, 1970. */
	time_t eventTime;
	/*! [in] Long term or short term job. */
	Duration persistent;
	int id;
};

/*!
 * A timer thread that allows the scheduling of jobs to run at a
   specified time in the future.
 *
 * Because the timer thread uses the thread pool there is no 
 * gurantee of timing, only approximate timing.
 *
 * Uses ThreadPool, Mutex, Condition, Thread.
 */
class TimerThread {
public:
	TimerThread(ThreadPool *tp);
	~TimerThread();

	/*!
	 * \brief Schedules an event to run at a specified time.
	 *
	 * \return 0 on success, nonzero on failure, EOUTOFMEM if not enough memory
	 * 	to schedule job.
	 */
	int schedule(
		/*! [in] time of event. Either in absolute seconds, or relative
		 * seconds in the future. */
		time_t time, 
		/*! [in] either ABS_SEC, or REL_SEC. If REL_SEC, then the event
		 * will be scheduled at the current time + REL_SEC. */
		TimeoutType type,
		/*! [in] Valid Thread pool job with following fields. */
		ThreadPoolJob *job,
		/*! [in] . */
		Duration duration,
		/*! [in] Id of timer event. (out, can be null). */
		int *id);

	/*!
	 * \brief Removes an event from the timer Q.
	 *
	 * Events can only be removed before they have been placed in the thread pool
	 *
	 * \return 0 on success, INVALID_EVENT_ID on failure.
	 */
	int remove(
		/*! [in] Id of event to remove. */
		int id,
		/*! [in] Space for thread pool job. */
		ThreadPoolJob *out);

	/*!
	 * \brief Shutdown the timer thread.
	 *
	 * Events scheduled in the future will NOT be run.
	 *
	 * Timer thread should be shutdown BEFORE it's associated thread pool.
	 *
	 * \return 0 if succesfull, nonzero otherwise. Always returns 0.
	 */
	int shutdown();

	friend void *TimerThreadWorker(void*);
private:
	void freeTimerEvent(TimerEvent *event);
	TimerEvent *CreateTimerEvent(ThreadPoolJob *, Duration, time_t, int);
	ithread_mutex_t mutex;
	ithread_cond_t condition;
	int lastEventId{0};
	std::list<TimerEvent*> eventQ;
	int inshutdown{0};
	FreeList<TimerEvent> freeEvents;
	ThreadPool *tp{nullptr};
};

#endif /* TIMER_THREAD_H */


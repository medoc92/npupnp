/*******************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation 
 * Copyright (c) 2012 France Telecom All rights reserved. 
 * Copyright (c) 2020 J.F. Dockes <jf@dockes.org>
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 * this list of conditions and the following disclaimer. 
 * - Redistributions in binary form must reproduce the above copyright notice, 
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution. 
 * - Neither name of Intel Corporation nor the names of its contributors 
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

#include "TimerThread.h"

#include <assert.h>
#include <chrono>
#include <mutex>
#include <condition_variable>

using namespace std::chrono;

/*!
 * Data holder for a timer event.
 * The destructor does not call the free_func: this is done by the ThreadPool,
 * or explicitely in here only in case of error (should do it also during 
 * shutdown actually, but this is never used anyway).
 */
struct TimerEvent {
	TimerEvent(
		start_routine f, void *a, ThreadPool::free_routine fr, 
		ThreadPool::ThreadPriority prio,
		TimerThread::Duration p, system_clock::time_point et, int _id)
		: func(f), arg(a), free_func(fr), priority(prio), persistent(p),
		  eventTime(et), id(_id) {}
	
	start_routine func;
	void *arg;
	ThreadPool::free_routine free_func;
	ThreadPool::ThreadPriority priority;
	/*! [in] Long term or short term job. */
	TimerThread::Duration persistent;
	/*! [in] Absolute time for event in seconds since Jan 1, 1970. */
	system_clock::time_point eventTime;
	int id;
};

class TimerThread::Internal {
public:
	Internal(ThreadPool *tp);
	
	std::mutex mutex;
	std::condition_variable condition;
	int lastEventId{0};
	std::list<TimerEvent*> eventQ;
	int inshutdown{0};
	ThreadPool *tp{nullptr};
};

/*!
 * \brief Implements timer thread.
 *
 * Waits for next event to occur and schedules associated job into threadpool.
 *	arg is cast to (TimerThread *).
 */
void *thread_timer(void *arg)
{
	auto timer = (TimerThread::Internal *)arg;
	TimerEvent *nextEvent = nullptr;
	system_clock::time_point nextEventTime = system_clock::now();

	assert(timer != nullptr);

	std::unique_lock<std::mutex> lck(timer->mutex);

	while (1) {
		/* mutex should always be locked at top of loop */
		/* Check for shutdown. */
		if (timer->inshutdown) {
			timer->inshutdown = 0;
			timer->condition.notify_all();
			return nullptr;
		}
		nextEvent = nullptr;
		/* Get the next event if possible. */
		if (!timer->eventQ.empty()) {
			nextEvent = timer->eventQ.front();
			nextEventTime = nextEvent->eventTime;
		}
		system_clock::time_point currentTime = system_clock::now();
		/* If time has elapsed, schedule job. */
		if (nextEvent && currentTime >= nextEventTime) {
			int ret = 0;
			if (nextEvent->persistent) {
				ret = timer->tp->addPersistent(
					nextEvent->func, nextEvent->arg, nextEvent->free_func,
					nextEvent->priority);
			} else {
				ret = timer->tp->addJob(
					nextEvent->func, nextEvent->arg, nextEvent->free_func,
					nextEvent->priority);
			}
			if (ret != 0 && nullptr != nextEvent->free_func) {
				nextEvent->free_func(nextEvent->arg);
			}
			timer->eventQ.pop_front();
			delete nextEvent;
			continue;
		}
		if (nextEvent) {
			timer->condition.wait_until(lck, nextEvent->eventTime);
		} else {
			timer->condition.wait(lck);
		}
	}
}


TimerThread::Internal::Internal(ThreadPool *tp)
{
	std::unique_lock<std::mutex> lck(mutex);
	this->tp = tp;
	tp->addPersistent(thread_timer, this, nullptr, ThreadPool::HIGH_PRIORITY);
}

TimerThread::TimerThread(ThreadPool *tp)
{
	assert( tp != nullptr );
	if (tp == nullptr ) {
		return;
	}
	m = new Internal(tp);
}

TimerThread::~TimerThread()
{
	delete m;
}

int TimerThread::schedule(
	Duration duration, TimeoutType type, time_t time, int *id,
	start_routine func, void *arg, ThreadPool::free_routine free_func, 
	ThreadPool::ThreadPriority priority
	)
{
	int rc = EOUTOFMEM;
	int found = 0;
	TimerEvent *newEvent = nullptr;

	system_clock::time_point when;
	if (type == TimerThread::ABS_SEC) {
		when = system_clock::from_time_t(time);
	} else {
		when = system_clock::now() + std::chrono::seconds(time);
	}

	std::unique_lock<std::mutex> lck(m->mutex);

	newEvent = new TimerEvent(func, arg, free_func, priority,
							  duration, when, m->lastEventId);
	if (newEvent == nullptr ) {
		return rc;
	}
	if (id) {
		*id = m->lastEventId;
	}
	/* add job to Q. Q is ordered by eventTime with the head of the Q being
	 * the next event. */
	rc = 0;
	for (auto it = m->eventQ.begin(); it != m->eventQ.end(); it++) {
		if ((*it)->eventTime >= when) {
			m->eventQ.insert(it, newEvent);
			found = 1;
			break;
		}
	}
	/* add to the end of Q. */
	if (!found) {
		m->eventQ.push_back(newEvent);
	}
	/* signal change in Q. */
	m->condition.notify_all();
	m->lastEventId++;

	return rc;
}

int TimerThread::remove(int id)
{
	int rc = -1;
	std::unique_lock<std::mutex> lck(m->mutex);

	for (auto it = m->eventQ.begin(); it != m->eventQ.end(); it++) {
		TimerEvent *temp = *it;
		if (temp->id == id) {
			m->eventQ.erase(it);
			delete temp;
			rc = 0;
			break;
		}
	}
	return rc;
}

int TimerThread::shutdown()
{
	std::unique_lock<std::mutex> lck(m->mutex);

	m->inshutdown = 1;

	/* Delete nodes in Q. Call registered free function on argument. */
	for (auto it = m->eventQ.begin(); it != m->eventQ.end(); it++) {
		delete *it;
	}
	m->eventQ.clear();

	m->condition.notify_all();

	while (m->inshutdown) {
		/* wait for timer thread to shutdown. */
		m->condition.wait(lck);
	}
	return 0;
}

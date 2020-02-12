/*******************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation 
 * Copyright (c) 2020 J.F. Dockes <jf@dockes.org>
 * All rights reserved. 
 * Copyright (c) 2012 France Telecom All rights reserved. 
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
		TimerThread::Duration p, time_t et, int _id)
		: func(f), arg(a), free_func(fr), priority(prio), persistent(p),
		  eventTime(et), id(_id) {}
	
	start_routine func;
	void *arg;
	ThreadPool::free_routine free_func;
	ThreadPool::ThreadPriority priority;
	/*! [in] Long term or short term job. */
	TimerThread::Duration persistent;
	/*! [in] Absolute time for event in seconds since Jan 1, 1970. */
	time_t eventTime;
	int id;
};


/*!
 * \brief Implements timer thread.
 *
 * Waits for next event to occur and schedules associated job into threadpool.
 *  arg is cast to (TimerThread *).
 */
void *timerThreadWorker(void *arg)
{
    TimerThread *timer = (TimerThread *)arg;
    TimerEvent *nextEvent = NULL;
    time_t currentTime = 0;
    time_t nextEventTime = 0;
    struct timespec timeToWait;

    assert( timer != NULL );

    ithread_mutex_lock(&timer->mutex);
    while (1) {
        /* mutex should always be locked at top of loop */
        /* Check for shutdown. */
        if (timer->inshutdown) {
            timer->inshutdown = 0;
            ithread_cond_signal( &timer->condition );
            ithread_mutex_unlock( &timer->mutex );
            return NULL;
        }
        nextEvent = NULL;
        /* Get the next event if possible. */
        if (timer->eventQ.size() > 0) {
            nextEvent = timer->eventQ.front();
            nextEventTime = nextEvent->eventTime;
        }
        currentTime = time(NULL);
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
            timeToWait.tv_nsec = 0;
            timeToWait.tv_sec = (long)nextEvent->eventTime;
            ithread_cond_timedwait(&timer->condition, &timer->mutex,
                                    &timeToWait);
        } else {
            ithread_cond_wait(&timer->condition, &timer->mutex);
        }
    }
}


/*!
 * \brief Calculates the appropriate timeout in absolute seconds
 * since Jan 1, 1970.
 *
 * \return 
 */
static int CalculateEventTime(
    /*! [in] Timeout. */
    time_t *timeout,
    /*! [in] Timeout type. */
    TimerThread::TimeoutType type)
{
    time_t now;

    switch (type) {
    case TimerThread::ABS_SEC:
        return 0;
    default: /* REL_SEC) */
        time(&now);
        *timeout += now;
        return 0;
    }

    return -1;
}

TimerThread::TimerThread(ThreadPool *tp)
{
    assert( tp != NULL );
    if (tp == NULL ) {
        return;
    }

    int rc = ithread_mutex_init(&this->mutex, NULL);
    assert( rc == 0 );
    rc += ithread_mutex_lock(&this->mutex);
    assert( rc == 0 );
    rc += ithread_cond_init(&this->condition, NULL);
    assert( rc == 0 );

    this->tp = tp;

    rc = tp->addPersistent(timerThreadWorker, this, nullptr,
						   ThreadPool::HIGH_PRIORITY);

    ithread_mutex_unlock(&this->mutex );

    if (rc != 0) {
        ithread_cond_destroy( &this->condition );
        ithread_mutex_destroy( &this->mutex );
    }

    return;
}

TimerThread::~TimerThread()
{
    /* destroy condition. */
    while (ithread_cond_destroy(&this->condition) != 0) {
    }
    /* destroy mutex. */
    while (ithread_mutex_destroy(&this->mutex) != 0) {
    }
}

int TimerThread::schedule(
		Duration duration, TimeoutType type, time_t time, int *id,
		start_routine func, void *arg, ThreadPool::free_routine free_func, 
		ThreadPool::ThreadPriority priority
	)
{
    int rc = EOUTOFMEM;
    int found = 0;
    TimerEvent *newEvent = NULL;
	time_t timeout{time};
	
    CalculateEventTime(&timeout, type);
    ithread_mutex_lock(&this->mutex);

    newEvent = new TimerEvent(func, arg, free_func, priority,
							  duration, timeout, this->lastEventId);
    if (newEvent == NULL ) {
        ithread_mutex_unlock( &this->mutex );
        return rc;
    }

    /* add job to Q. Q is ordered by eventTime with the head of the Q being
     * the next event. */
	rc = 0;
	for (auto it = eventQ.begin(); it != eventQ.end(); it++) {
        if ((*it)->eventTime >= timeout) {
            eventQ.insert(it, newEvent);
            found = 1;
            break;
        }
    }
    /* add to the end of Q. */
    if (!found) {
        eventQ.push_back(newEvent);
    }
    /* signal change in Q. */
	ithread_cond_signal( &this->condition );
    this->lastEventId++;
    ithread_mutex_unlock(&this->mutex);

    return rc;
}

int TimerThread::remove(int id)
{
    int rc = -1;
    ithread_mutex_lock( &this->mutex );

	for (auto it = eventQ.begin(); it != eventQ.end(); it++) {
		TimerEvent *temp = *it;
        if (temp->id == id) {
			eventQ.erase(it);
            delete temp;
            rc = 0;
            break;
        }
    }

    ithread_mutex_unlock( &this->mutex );
    return rc;
}

int TimerThread::shutdown()
{
    ithread_mutex_lock( &this->mutex );

    this->inshutdown = 1;

    /* Delete nodes in Q. Call registered free function on argument. */
	for (auto it = eventQ.begin(); it != eventQ.end(); it++) {
        delete *it;
    }
	eventQ.clear();

    ithread_cond_broadcast(&this->condition);

    while (this->inshutdown) {
        /* wait for timer thread to shutdown. */
        ithread_cond_wait(&this->condition, &this->mutex);
    }
    ithread_mutex_unlock(&this->mutex);
	return 0;
}

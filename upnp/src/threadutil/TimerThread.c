/*******************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation 
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

/*!
 * \file
 */

#include "TimerThread.h"

#include <assert.h>

/*!
 * \brief Deallocates a dynamically allocated TimerEvent.
 */
void TimerThread::freeTimerEvent(
    /*! [in] Must be allocated with CreateTimerEvent*/
    TimerEvent *event)
{
    freeEvents.flfree(event);
}

/*!
 * \brief Implements timer thread.
 *
 * Waits for next event to occur and schedules associated job into threadpool.
 */
void *TimerThreadWorker(
    /*! [in] arg is cast to (TimerThread *). */
    void *arg)
{
    TimerThread *timer = ( TimerThread * ) arg;
    TimerEvent *nextEvent = NULL;
    time_t currentTime = 0;
    time_t nextEventTime = 0;
    struct timespec timeToWait;
    int tempId;

    assert( timer != NULL );

    ithread_mutex_lock( &timer->mutex );
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
            if( nextEvent->persistent ) {
                if (ThreadPoolAddPersistent(timer->tp, &nextEvent->job,
                                            &tempId ) != 0) {
                    if (nextEvent->job.arg != NULL &&
						nextEvent->job.free_func != NULL) {
                        nextEvent->job.free_func(nextEvent->job.arg);
                    }
                }
            } else {
                if (ThreadPoolAdd( timer->tp, &nextEvent->job, &tempId ) != 0) {
                    if (nextEvent->job.arg != NULL &&
						nextEvent->job.free_func != NULL) {
                        nextEvent->job.free_func(nextEvent->job.arg);
                    }
                }
            }
            timer->eventQ.pop_front();
            timer->freeTimerEvent(nextEvent);
            continue;
        }
        if (nextEvent) {
            timeToWait.tv_nsec = 0;
            timeToWait.tv_sec = (long)nextEvent->eventTime;
            ithread_cond_timedwait( &timer->condition, &timer->mutex,
                                    &timeToWait );
        } else {
            ithread_cond_wait( &timer->condition, &timer->mutex );
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
    TimeoutType type)
{
    time_t now;

    assert( timeout != NULL );

    switch (type) {
    case ABS_SEC:
        return 0;
    default: /* REL_SEC) */
        time(&now);
        ( *timeout ) += now;
        return 0;
    }

    return -1;
}

/*!
 * \brief Creates a Timer Event. (Dynamically allocated).
 *
 * \return (TimerEvent *) on success, NULL on failure.
 */
TimerEvent *TimerThread::CreateTimerEvent(
    /*! [in] . */
    ThreadPoolJob *job,
    /*! [in] . */
    Duration persistent,
    /*! [in] The absoule time of the event in seconds from Jan, 1970. */
    time_t eventTime,
    /*! [in] Id of job. */
    int id)
{
    assert( job != NULL );

    TimerEvent *temp = freeEvents.flalloc();
    if (temp == NULL)
        return temp;
    temp->job = ( *job );
    temp->persistent = persistent;
    temp->eventTime = eventTime;
    temp->id = id;

    return temp;
}


TimerThread::TimerThread(ThreadPool *tp)
: freeEvents(100)
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

    ThreadPoolJob timerThreadWorker;
	TPJobInit(&timerThreadWorker, TimerThreadWorker, this);
	TPJobSetPriority(&timerThreadWorker, HIGH_PRIORITY);
    rc = ThreadPoolAddPersistent(tp, &timerThreadWorker, NULL);

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
    time_t timeout,
    TimeoutType type,
    ThreadPoolJob *job,
    Duration duration,
    int *id)
{
    int rc = EOUTOFMEM;
    int found = 0;
    int tempId = 0;
    TimerEvent *newEvent = NULL;

    assert( job != NULL );
    if (job == NULL) {
        return EINVAL;
    }

    CalculateEventTime( &timeout, type );
    ithread_mutex_lock( &this->mutex );

    if( id == NULL )
        id = &tempId;

    ( *id ) = INVALID_EVENT_ID;

    newEvent = CreateTimerEvent(job, duration, timeout, this->lastEventId);

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
    *id = this->lastEventId++;
    ithread_mutex_unlock(&this->mutex);

    return rc;
}

int TimerThread::remove(int id, ThreadPoolJob *out)
{
    int rc = INVALID_EVENT_ID;
    ithread_mutex_lock( &this->mutex );

	for (auto it = eventQ.begin(); it != eventQ.end(); it++) {
		TimerEvent *temp = *it;
        if (temp->id == id) {
			eventQ.erase(it);
            if (out != NULL)
                *out = temp->job;
            freeTimerEvent(temp);
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
        TimerEvent *temp = *it;
        if (temp->job.free_func) {
            temp->job.free_func(temp->job.arg);
        }
        freeTimerEvent(temp);
    }
	eventQ.clear();
    freeEvents.clear();

    ithread_cond_broadcast(&this->condition);

    while (this->inshutdown) {
        /* wait for timer thread to shutdown. */
        ithread_cond_wait(&this->condition, &this->mutex);
    }
    ithread_mutex_unlock(&this->mutex);
	return 0;
}

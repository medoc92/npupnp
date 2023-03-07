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

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>

using namespace std::chrono;

/*! Data holder for a timer event. */
struct TimerEvent {
    TimerEvent(
        std::unique_ptr<JobWorker> w, ThreadPool::ThreadPriority prio,
        TimerThread::Duration p, system_clock::time_point et, int _id)
        : priority(prio), persistent(p), eventTime(et), id(_id) {
        worker = std::move(w);
    }
    
    std::unique_ptr<JobWorker> worker;
    ThreadPool::ThreadPriority priority;
    /*! [in] Long term or short term job. */
    TimerThread::Duration persistent;
    /*! [in] Absolute time for event in seconds since Jan 1, 1970. */
    system_clock::time_point eventTime;
    int id;
};


class TimerJobWorker : public JobWorker {
public:
    explicit TimerJobWorker(TimerThread::Internal* parent)
        : m_parent(parent) {}
    void work() override;
    TimerThread::Internal *m_parent;
};

class TimerThread::Internal {
public:
    explicit Internal(ThreadPool *tp);
    virtual ~Internal() = default;
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
 *    arg is cast to (TimerThread *).
 */
void TimerJobWorker::work()
{
    auto timer = m_parent;
    TimerEvent *nextEvent = nullptr;
    system_clock::time_point nextEventTime = system_clock::now();

    assert(timer != nullptr);

    std::unique_lock<std::mutex> lck(timer->mutex);

    while (true) {
        /* mutex should always be locked at top of loop */
        /* Check for shutdown. */
        if (timer->inshutdown) {
            timer->inshutdown = 0;
            timer->condition.notify_all();
            return;
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
            if (nextEvent->persistent) {
                timer->tp->addPersistent(std::move(nextEvent->worker), nextEvent->priority);
            } else {
                timer->tp->addJob(std::move(nextEvent->worker), nextEvent->priority);
            }
            timer->eventQ.pop_front();
            delete nextEvent;
            continue;
        }
        if (nextEvent) {
            auto tm = nextEvent->eventTime;
            timer->condition.wait_until(lck, tm);
        } else {
            timer->condition.wait(lck);
        }
    }
}

TimerThread::Internal::Internal(ThreadPool *tp)
{
    std::unique_lock<std::mutex> lck(mutex);
    this->tp = tp;
    auto worker = std::make_unique<TimerJobWorker>(this);
    tp->addPersistent(std::move(worker), ThreadPool::HIGH_PRIORITY);
}

TimerThread::TimerThread(ThreadPool *tp)
{
    assert(tp != nullptr);
    if (nullptr == tp) {
        return;
    }
    m = std::make_unique<Internal>(tp);
}

TimerThread::~TimerThread() = default;

int TimerThread::schedule(
    Duration persistence, std::chrono::system_clock::time_point when, int *id,
    std::unique_ptr<JobWorker> worker, ThreadPool::ThreadPriority priority
    )
{
    std::unique_lock<std::mutex> lck(m->mutex);
    int rc = EOUTOFMEM;

    auto newEvent = new TimerEvent(std::move(worker), priority, persistence, when, m->lastEventId);
    if (newEvent == nullptr) {
        return rc;
    }
    if (id) {
        *id = m->lastEventId;
    }
    /* add job to Q. Q is ordered by eventTime with the head of the Q being
     * the next event. */
    rc = 0;
    int found = 0;
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

int TimerThread::schedule(
    Duration persistence, std::chrono::milliseconds delay, int *id,
    std::unique_ptr<JobWorker> worker, ThreadPool::ThreadPriority priority
    )
{
    system_clock::time_point when;
    when = system_clock::now() + delay;
    return TimerThread::schedule(persistence, when, id, std::move(worker), priority);
}

int TimerThread::schedule(
    Duration persistence, TimeoutType type, time_t time, int *id,
    std::unique_ptr<JobWorker> worker, ThreadPool::ThreadPriority priority
    )
{
    system_clock::time_point when;
    if (type == TimerThread::ABS_SEC) {
        when = system_clock::from_time_t(time);
    } else {
        when = system_clock::now() + std::chrono::seconds(time);
    }
    return TimerThread::schedule(persistence, when, id, std::move(worker), priority);
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
    for (auto & it : m->eventQ) {
        delete it;
    }
    m->eventQ.clear();

    m->condition.notify_all();

    while (m->inshutdown) {
        /* wait for timer thread to shutdown. */
        m->condition.wait(lck);
    }
    return 0;
}

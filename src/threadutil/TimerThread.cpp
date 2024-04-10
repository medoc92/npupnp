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

#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <list>
#include <mutex>
#include <utility>

using namespace std::chrono;

/*! Data holder for a timer event. */
struct TimerEvent {
    TimerEvent(
        std::unique_ptr<JobWorker> w, ThreadPool::ThreadPriority prio,
        TimerThread::Duration p, system_clock::time_point et, int _id)
        : worker(std::move(w)), eventTime(et), id(_id), priority(prio), persistent(p)
    {
    }

    std::unique_ptr<JobWorker> worker;
    /*! [in] Absolute time for event in seconds since Jan 1, 1970. */
    system_clock::time_point eventTime;
    int id;
    ThreadPool::ThreadPriority priority;
    /*! [in] Long term or short term job. */
    TimerThread::Duration persistent;
};


// This is the worker for the permanent timer thread, in charge of dispatching jobs at appropriate
// times
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
    std::list<TimerEvent> eventQ;
    int inshutdown{0};
    ThreadPool *tp{nullptr};
};

/*!
 * \brief Implements timer thread.
 *
 * tl;dr: Sleeps until next event scheduled time, then dispatches it then sleeps...
 */
void TimerJobWorker::work()
{
    auto timer = m_parent;
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
        system_clock::time_point currentTime = system_clock::now();
        /* Get the next event if possible. */
        if (!timer->eventQ.empty()) {
            TimerEvent& nextEvent(timer->eventQ.front());
            if (currentTime >= nextEvent.eventTime) {
                /* If time has elapsed, schedule job. */
                if (timer->eventQ.front().persistent) {
                    timer->tp->addPersistent(std::move(nextEvent.worker), nextEvent.priority);
                } else {
                    timer->tp->addJob(std::move(nextEvent.worker), nextEvent.priority);
                }
                timer->eventQ.pop_front();
            } else {
                auto tm = nextEvent.eventTime;
                timer->condition.wait_until(lck, tm);
            }
        } else {
            timer->condition.wait(lck);
        }
    }
}

TimerThread::Internal::Internal(ThreadPool *tp)
{
    std::scoped_lock lck(mutex);
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
    std::scoped_lock lck(m->mutex);

    if (id) {
        *id = m->lastEventId;
    }
    /* add job to Q. Q is ordered by eventTime with the head of the Q being
     * the next event. */
    auto it = std::find_if(m->eventQ.begin(), m->eventQ.end(),
                           [=](const auto& e) { return e.eventTime >= when; });
    if (it != m->eventQ.end())
        m->eventQ.emplace(it, std::move(worker), priority, persistence, when, m->lastEventId);
    else
        m->eventQ.emplace_back(std::move(worker), priority, persistence, when, m->lastEventId);

    /* signal change in Q. */
    m->condition.notify_all();
    m->lastEventId++;
    return 0;
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
    std::scoped_lock lck(m->mutex);

    auto it = std::find_if(m->eventQ.begin(), m->eventQ.end(),
                           [id](const auto& e) { return e.id == id; });
    if (it != m->eventQ.end()) {
        m->eventQ.erase(it);
        return 0;
    }

    return -1;
}

int TimerThread::shutdown()
{
    std::unique_lock<std::mutex> lck(m->mutex);

    m->inshutdown = 1;
    m->eventQ.clear();
    m->condition.notify_all();

    while (m->inshutdown) {
        /* wait for timer thread to shutdown. */
        m->condition.wait(lck);
    }
    return 0;
}

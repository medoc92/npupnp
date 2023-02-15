/*******************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation
 * All rights reserved.
 * Copyright (c) 2012 France Telecom All rights reserved.
 * Copyright (c) 2020 J.F. Dockes <jf@dockes.org>
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

#include "ThreadPool.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>

#if defined(__OSX__) || defined(__APPLE__) || defined(__NetBSD__)
#include <sys/resource.h>
#endif

#include <chrono>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <iostream>

using namespace std::chrono;

// #define THREADPOOL_DEBUG
#define LOGERR(X) do {                                              \
        std::cerr << X;                                             \
    } while (0)
#ifdef THREADPOOL_DEBUG
#define LOGDEB(X) LOGERR(X)
#else
#define LOGDEB(X)
#endif

/*! Internal ThreadPool Job. */
struct ThreadPoolJob {
    ThreadPoolJob(std::unique_ptr<JobWorker> worker, ThreadPool::ThreadPriority _prio)
        : m_worker(std::move(worker)), priority(_prio) {}
    ~ThreadPoolJob() = default;
    std::unique_ptr<JobWorker> m_worker;
    ThreadPool::ThreadPriority priority;
    steady_clock::time_point requestTime;
    int jobId;
};

class ThreadPool::Internal {
public:
    explicit Internal(ThreadPoolAttr *attr);
    bool ok{false};
    int createWorker(std::unique_lock<std::mutex>& lck);
    void addWorker(std::unique_lock<std::mutex>& lck);
    void StatsAccountLQ(int64_t diffTime);
    void StatsAccountMQ(int64_t diffTime);
    void StatsAccountHQ(int64_t diffTime);
    void CalcWaitTime(ThreadPriority p, const std::unique_ptr<ThreadPoolJob>& job);
    void bumpPriority();
    void WorkerThread();
    int shutdown();

    /*! Mutex to protect job qs. */
    std::mutex mutex;
    /*! Condition variable to signal Q. */
    std::condition_variable condition;
    /*! Condition variable for start and stop. */
    std::condition_variable start_and_shutdown;

    /*! ids for jobs */
    int lastJobId;
    /*! whether or not we are shutting down */
    bool shuttingdown;
    /*! total number of threads */
    int totalThreads;
    /*! flag that's set when waiting for a new worker thread to start */
    int pendingWorkerThreadStart;
    /*! number of threads that are currently executing jobs */
    int busyThreads;
    /*! number of persistent threads */
    int persistentThreads;
    /*! low priority job Q */
    std::deque<std::unique_ptr<ThreadPoolJob>> lowJobQ;
    /*! med priority job Q */
    std::deque<std::unique_ptr<ThreadPoolJob>> medJobQ;
    /*! high priority job Q */
    std::deque<std::unique_ptr<ThreadPoolJob>> highJobQ;
    /*! persistent job */
    std::unique_ptr<ThreadPoolJob> persistentJob;
    /*! thread pool attributes */
    ThreadPoolAttr attr;
    /*! statistics */
    ThreadPoolStats stats;
};

ThreadPool::ThreadPool() = default;

ThreadPool::~ThreadPool()
{
    // JFD: Doing a proper shutdown does not work at the moment. One
    // of the threads does not exit. I suspect it's the timer thread
    // (not quite sure), but we have no way to signal it. For this
    // stuff to work, any permanent thread should poll for an exit
    // event, not the case at this point. I suspect that the original
    // design is wrong: the persistent threads are probably not
    // compatible with the shutdown() routine.  This is no big deal,
    // because I can't think of a process which would want to shutdown
    // its UPnP service and do something else further on... Going to
    // exit anyway. Actually calling _exit() might be the smart thing here :)
#if 0
    shutdown();
#endif
    // Also actually deleting the pool under these conditions blocks or crashes
    m.release();
}

int ThreadPool::start(ThreadPoolAttr *attr)
{
    m = std::make_unique<Internal>(attr);
    if (m && m->ok) {
        return 0;
    }
    return -1;
}

void ThreadPool::Internal::StatsAccountLQ(int64_t diffTime)
{
    this->stats.totalJobsLQ++;
    this->stats.totalTimeLQ += static_cast<double>(diffTime);
}

void ThreadPool::Internal::StatsAccountMQ(int64_t diffTime)
{
    this->stats.totalJobsMQ++;
    this->stats.totalTimeMQ += static_cast<double>(diffTime);
}

void ThreadPool::Internal::StatsAccountHQ(int64_t diffTime)
{
    this->stats.totalJobsHQ++;
    this->stats.totalTimeHQ += static_cast<double>(diffTime);
}

/*!
 * \brief Calculates the time the job has been waiting at the specified
 * priority.
 *
 * Adds to the totalTime and totalJobs kept in the thread pool statistics
 * structure.
 *
 * \internal
 */
void ThreadPool::Internal::CalcWaitTime(ThreadPriority p, const std::unique_ptr<ThreadPoolJob>& job)
{
    assert(job != nullptr);

    auto now = steady_clock::now();
    auto ms =
        duration_cast<milliseconds>(now - job->requestTime);
    auto diff = ms.count();
    switch (p) {
    case LOW_PRIORITY:
        StatsAccountLQ(diff);
        break;
    case MED_PRIORITY:
        StatsAccountMQ(diff);
        break;
    case HIGH_PRIORITY:
        StatsAccountHQ(diff);
        break;
    default:
        assert(0);
    }
}

/*!
 * \brief Sets the scheduling policy of the current process.
 *
 * \internal
 *
 * \return
 *     \li \c 0 on success.
 *      \li \c result of GetLastError() on failure.
 *
 */
static int SetPolicyType(ThreadPoolAttr::PolicyType in)
{
    int retVal = 0;
    ((void)(in));

#ifdef __CYGWIN__
    /* TODO not currently working... */
    retVal = 0;
#elif defined(__OSX__) || defined(__APPLE__) || defined(__NetBSD__)
    setpriority(PRIO_PROCESS, 0, 0);
    retVal = 0;
#elif defined(_MSC_VER)
//    retVal = sched_setscheduler(0, in);
    retVal = 0;
#elif defined(_POSIX_PRIORITY_SCHEDULING) && _POSIX_PRIORITY_SCHEDULING > 0
    struct sched_param current = {};
    int sched_result;
    sched_getparam(0, &current);
    // At the moment, in is always SCHED_OTHER, and priority must be 0 (see sched(7))
    current.sched_priority = 0;
    sched_result = sched_setscheduler(0, in, &current);
    retVal = (sched_result != -1 || errno == EPERM) ? 0 : errno;
#else
    retVal = 0;
#endif
    return retVal;
}

/*
 * Sets the priority of the currently running thread.
 *
 * Note that this probably never does anything as the sched type is always SCHED_OTHER which
 * does not use priorities (must be 0). Also, I've never seen _POSIX_PRIORITY_SCHEDULING
 * defined except on a very weird system, so the method is generally empty.
 *
 * @return 0 on success, else EINVAL invalid priority or errno.
 */
static int SetPriority(ThreadPool::ThreadPriority priority)
{
#if defined(_POSIX_PRIORITY_SCHEDULING) && _POSIX_PRIORITY_SCHEDULING > 0
    int retVal = 0;
    int currentPolicy;
    int minPriority = 0;
    int maxPriority = 0;
    int actPriority = 0;
    int midPriority = 0;
    struct sched_param newPriority;
    int sched_result;

    pthread_getschedparam(pthread_self(), &currentPolicy, &newPriority);
    minPriority = sched_get_priority_min(currentPolicy);
    maxPriority = sched_get_priority_max(currentPolicy);
    midPriority = (maxPriority - minPriority) / 2;
    switch (priority) {
    case ThreadPool::LOW_PRIORITY:
        actPriority = minPriority;
        break;
    case ThreadPool::MED_PRIORITY:
        actPriority = midPriority;
        break;
    case ThreadPool::HIGH_PRIORITY:
        actPriority = maxPriority;
        break;
    default:
        retVal = EINVAL;
        goto exit_function;
    };

    newPriority.sched_priority = actPriority;

    sched_result = pthread_setschedparam(pthread_self(), currentPolicy, &newPriority);
    retVal = (sched_result == 0 || errno == EPERM) ? 0 : sched_result;
exit_function:
    return retVal;
#else
    (void)priority;
    return 0;
#endif
}

/*!
 * \brief Determines whether any jobs need to be bumped to a higher priority Q
 * and bumps them.
 *
 * mutex must be locked.
 *
 * \internal
 *
 * \return
 */
void ThreadPool::Internal::bumpPriority()
{
    int done = 0;
    auto now = steady_clock::now();

    while (!done) {
        if (!medJobQ.empty()) {
            auto diffTime = duration_cast<milliseconds>(now - medJobQ.front()->requestTime).count();
            if (diffTime >= attr.starvationTime) {
                /* If job has waited longer than the starvation time
                 * bump priority (add to higher priority Q) */
                StatsAccountMQ(diffTime);
                highJobQ.push_back(std::move(medJobQ.front()));
                medJobQ.pop_front();
                continue;
            }
        }
        if (!lowJobQ.empty()) {
            auto diffTime = duration_cast<milliseconds>(now - lowJobQ.front()->requestTime).count();
            if (diffTime >= attr.maxIdleTime) {
                /* If job has waited longer than the starvation time
                 * bump priority (add to higher priority Q) */
                StatsAccountLQ(diffTime);
                medJobQ.push_back(std::move(lowJobQ.front()));
                lowJobQ.pop_front();
                continue;
            }
        }
        done = 1;
    }
}

/*
 * \brief Sets seed for random number generator. Each thread sets the seed
 * random number generator. */
static void SetSeed()
{
    const auto p1 = std::chrono::system_clock::now();
    auto cnt = p1.time_since_epoch().count();
    // Keep the nanoseconds
    cnt = cnt % 1000000000;
    std::hash<std::thread::id> id_hash;
    size_t h = id_hash(std::this_thread::get_id());
    srand(static_cast<unsigned int>(cnt+h));
}

/*!
 * \brief Implements a thread pool worker. Worker waits for a job to become
 * available. Worker picks up persistent jobs first, high priority,
 * med priority, then low priority.
 *
 * If worker remains idle for more than specified max, the worker is released.
 */
void ThreadPool::Internal::WorkerThread() {
    time_t start = 0;
    std::unique_ptr<ThreadPoolJob> job;
    std::cv_status retCode;
    int persistent = -1;

    std::unique_lock<std::mutex> lck(mutex, std::defer_lock);
    auto idlemillis = std::chrono::milliseconds(attr.maxIdleTime);

    /* Increment total thread count */
    lck.lock();
    totalThreads++;
    pendingWorkerThreadStart = 0;
    start_and_shutdown.notify_all();
    lck.unlock();

    SetSeed();
    start = time(nullptr);
    while (true) {
        lck.lock();
        if (job) {
            busyThreads--;
            job = nullptr;
        }
        stats.idleThreads++;
        stats.totalWorkTime += static_cast<double>(time(nullptr)) - static_cast<double>(start);
        start = time(nullptr);
        if (persistent == 0) {
            stats.workerThreads--;
        } else if (persistent == 1) {
            /* Persistent thread becomes a regular thread */
            persistentThreads--;
        }

        /* Check for a job or shutdown */
        retCode = std::cv_status::no_timeout;
        while (lowJobQ.empty() &&
               medJobQ.empty() &&
               highJobQ.empty() &&
               !persistentJob && !shuttingdown) {
            /* If wait timed out and we currently have more than the
             * min threads, or if we have more than the max threads
             * (only possible if the attributes have been reset)
             * let this thread die. */
            if ((retCode == std::cv_status::timeout &&
                 totalThreads > attr.minThreads) ||
                (attr.maxThreads != -1 &&
                 totalThreads > attr.maxThreads)) {
                stats.idleThreads--;
                goto exit_function;
            }

            /* wait for a job up to the specified max time */
            retCode = condition.wait_for(lck, idlemillis);
        }

        stats.idleThreads--;
        /* idle time */
        stats.totalIdleTime += static_cast<double>(time(nullptr)) - static_cast<double>(start);
        /* work time */
        start = time(nullptr);
        /* bump priority of starved jobs */
        bumpPriority();
        /* if shutdown then stop */
        if (shuttingdown) {
            goto exit_function;
        } else {
            /* Pick up persistent job if available */
            if (persistentJob) {
                job = std::move(persistentJob);
                persistentThreads++;
                persistent = 1;
                start_and_shutdown.notify_all();
            } else {
                stats.workerThreads++;
                persistent = 0;
                /* Pick the highest priority job */
                if (!highJobQ.empty()) {
                    job = std::move(highJobQ.front());
                    highJobQ.pop_front();
                    CalcWaitTime(ThreadPool::HIGH_PRIORITY, job);
                } else if (!medJobQ.empty()) {
                    job = std::move(medJobQ.front());
                    medJobQ.pop_front();
                    CalcWaitTime(ThreadPool::MED_PRIORITY, job);
                } else if (!lowJobQ.empty()) {
                    job = std::move(lowJobQ.front());
                    lowJobQ.pop_front();
                    CalcWaitTime(ThreadPool::LOW_PRIORITY, job);
                } else {
                    /* Should never get here */
                    stats.workerThreads--;
                    goto exit_function;
                }
            }
        }

        busyThreads++;
        lck.unlock();

        SetPriority(job->priority);
        /* run the job */
        job->m_worker->work();
        /* return to Normal */
        SetPriority(ThreadPool::MED_PRIORITY);
    }

exit_function:
    LOGDEB("ThreadWorker: thread exiting\n");
    totalThreads--;
    start_and_shutdown.notify_all();
}

/*!
 * \brief Creates a worker thread, if the thread pool does not already have
 * max threads.
 *
 * \remark The ThreadPool object mutex must be locked prior to calling this
 * function.
 *
 * \internal
 *
 * \return
 *    \li \c 0 on success, < 0 on failure.
 *    \li \c EMAXTHREADS if already max threads reached.
 *    \li \c EAGAIN if system can not create thread.
 */
int ThreadPool::Internal::createWorker(std::unique_lock<std::mutex>& lck)
{
    /* if a new worker is the process of starting, wait until it fully starts */
    while (this->pendingWorkerThreadStart) {
        this->start_and_shutdown.wait(lck);
    }

    if (this->attr.maxThreads != ThreadPoolAttr::INFINITE_THREADS &&
        this->totalThreads + 1 > this->attr.maxThreads) {
        LOGDEB("ThreadPool::createWorker: not creating thread: too many\n");
        return EMAXTHREADS;
    }
    LOGDEB("ThreadPool::createWorker: creating thread\n");
    auto nthread = std::thread([this] { WorkerThread(); });
    nthread.detach();

    /* wait until the new worker thread starts. We can set the flag
       cause we have the lock */
    this->pendingWorkerThreadStart = 1;
    while (this->pendingWorkerThreadStart) {
        this->start_and_shutdown.wait(lck);
    }

    if (this->stats.maxThreads < this->totalThreads) {
        this->stats.maxThreads = this->totalThreads;
    }

    return 0;
}

/*!
 * \brief Determines whether or not a thread should be added based on the
 * jobsPerThread ratio. Adds a thread if appropriate.
 *
 * \remark The ThreadPool object mutex must be locked prior to calling this
 * function.
 *
 */
void ThreadPool::Internal::addWorker(std::unique_lock<std::mutex>& lck)
{
    long jobs = highJobQ.size() + lowJobQ.size() + medJobQ.size();
    int threads = totalThreads - persistentThreads;
    LOGDEB("ThreadPool::addWorker: jobs: " << jobs << " threads: "<< threads <<
           " busyThr: " << busyThreads << " jobsPerThread: " <<
           attr.jobsPerThread << "\n");
    while (threads == 0 ||
           (jobs / threads) >= attr.jobsPerThread ||
           (totalThreads == busyThreads) ) {
        if (createWorker(lck) != 0) {
            return;
        }
        threads++;
    }
}

ThreadPool::Internal::Internal(ThreadPoolAttr *attr)
{
    int retCode = 0;
    int i = 0;

    std::unique_lock<std::mutex> lck(this->mutex);
    if (attr) {
        this->attr = *attr;
    }
    if (SetPolicyType(this->attr.schedPolicy) != 0) {
        return;
    }
    this->stats = ThreadPoolStats();
    this->persistentJob = nullptr;
    this->lastJobId = 0;
    this->shuttingdown = false;
    this->totalThreads = 0;
    this->busyThreads = 0;
    this->persistentThreads = 0;
    this->pendingWorkerThreadStart = 0;
    for (i = 0; i < this->attr.minThreads; ++i) {
        retCode = createWorker(lck);
        if (retCode) {
            break;
        }
    }

    lck.unlock();

    if (retCode) {
        /* clean up if the min threads could not be created */
        this->shutdown();
    } else {
        ok = true;
    }
}

int ThreadPool::addPersistent(std::unique_ptr<JobWorker> worker, ThreadPriority priority)
{
    std::unique_lock<std::mutex> lck(m->mutex);

    /* Create A worker if less than max threads running */
    if (m->totalThreads < m->attr.maxThreads) {
        m->createWorker(lck);
    } else {
        /* if there is more than one worker thread
         * available then schedule job, otherwise fail */
        if (m->totalThreads - m->persistentThreads - 1 == 0)
            return EMAXTHREADS;
    }

    auto job = std::make_unique<ThreadPoolJob>(std::move(worker), priority);
    job->jobId = m->lastJobId;
    job->requestTime = steady_clock::now();
    m->persistentJob = std::move(job);

    /* Notify a waiting thread */
    m->condition.notify_one();

    /* wait until long job has been picked up */
    while (m->persistentJob)
        m->start_and_shutdown.wait(lck);
    m->lastJobId++;

    return 0;
}

int ThreadPool::addJob(std::unique_ptr<JobWorker> worker, ThreadPriority prio)
{
    std::unique_lock<std::mutex> lck(m->mutex);

    int totalJobs = m->highJobQ.size() + m->lowJobQ.size() + m->medJobQ.size();
    if (totalJobs >= m->attr.maxJobsTotal) {
        LOGERR("ThreadPool::addJob: too many jobs: " << totalJobs << "\n");
        return 0;
    }

    auto job = std::make_unique<ThreadPoolJob>(std::move(worker), prio);
    job->jobId =  m->lastJobId;
    job->requestTime = steady_clock::now();
    switch (job->priority) {
    case HIGH_PRIORITY:
        m->highJobQ.push_back(std::move(job));
        break;
    case MED_PRIORITY:
        m->medJobQ.push_back(std::move(job));
        break;
    default:
        m->lowJobQ.push_back(std::move(job));
    }
    /* AddWorker if appropriate */
    m->addWorker(lck);
    /* Notify a waiting thread */
    m->condition.notify_one();
    m->lastJobId++;

    return 0;
}

int ThreadPool::getAttr(ThreadPoolAttr *out)
{
    if (!out)
        return EINVAL;
    if (!m->shuttingdown)
        m->mutex.lock();
    *out = m->attr;
    if (!m->shuttingdown)
        m->mutex.unlock();

    return 0;
}

int ThreadPool::setAttr(ThreadPoolAttr *attr)
{
    int retCode = 0;
    ThreadPoolAttr temp;
    int i = 0;

    std::unique_lock<std::mutex> lck(m->mutex);

    if (attr)
        temp = *attr;
    if (SetPolicyType(temp.schedPolicy) != 0) {
        return INVALID_POLICY;
    }
    m->attr = temp;
    /* add threads */
    if (m->totalThreads < m->attr.minThreads) {
        for (i = m->totalThreads; i < m->attr.minThreads; i++) {
            retCode = m->createWorker(lck);
            if (retCode != 0) {
                break;
            }
        }
    }
    /* signal changes */
    m->condition.notify_all();
    lck.unlock();

    if (retCode != 0)
        /* clean up if the min threads could not be created */
        m->shutdown();

    return retCode;
}

int ThreadPool::shutdown()
{
    if (m)
        return m->shutdown();
    return -1;
}

int ThreadPool::Internal::shutdown()
{
    std::unique_lock<std::mutex> lck(mutex);

    this->highJobQ.clear();
    this->medJobQ.clear();
    this->lowJobQ.clear();

    /* clean up long term job */
    if (this->persistentJob) {
        this->persistentJob = nullptr;
    }
    /* signal shutdown */
    this->shuttingdown = true;
    this->condition.notify_all();
    /* wait for all threads to finish */
    while (this->totalThreads > 0) {
        this->start_and_shutdown.wait(lck);
    }

    return 0;
}

int ThreadPool::getStats(ThreadPoolStats *stats)
{
    if (nullptr == stats)
        return EINVAL;
    /* if not shutdown then acquire mutex */
    std::unique_lock<std::mutex> lck(m->mutex, std::defer_lock);
    if (!m->shuttingdown)
        lck.lock();

    *stats = m->stats;
    if (stats->totalJobsHQ > 0)
        stats->avgWaitHQ = stats->totalTimeHQ / static_cast<double>(stats->totalJobsHQ);
    else
        stats->avgWaitHQ = 0.0;
    if (stats->totalJobsMQ > 0)
        stats->avgWaitMQ = stats->totalTimeMQ / static_cast<double>(stats->totalJobsMQ);
    else
        stats->avgWaitMQ = 0.0;
    if (stats->totalJobsLQ > 0)
        stats->avgWaitLQ = stats->totalTimeLQ / static_cast<double>(stats->totalJobsLQ);
    else
        stats->avgWaitLQ = 0.0;
    stats->totalThreads = m->totalThreads;
    stats->persistentThreads = m->persistentThreads;
    stats->currentJobsHQ = static_cast<int>(m->highJobQ.size());
    stats->currentJobsLQ = static_cast<int>(m->lowJobQ.size());
    stats->currentJobsMQ = static_cast<int>(m->medJobQ.size());

    return 0;
}

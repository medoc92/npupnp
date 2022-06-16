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

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <string>

#include <errno.h>

/* Errors. The old code had a bizarre expression which resulted in practise
 *  in all errors having the same value */
#define EOUTOFMEM -1
#define EMAXTHREADS -2
#define INVALID_POLICY -3

typedef void *(*start_routine)(void*);

/* Attributes for thread pool. Used to set and change parameters. */
#ifndef SCHED_OTHER
#define SCHED_OTHER 0
#endif
struct ThreadPoolAttr {
    typedef int PolicyType;
    enum TPSpecialValues{INFINITE_THREADS = -1};

    /*! ThreadPool will always maintain at least this many threads. */
    int minThreads{1};
    /*! ThreadPool will never have more than this number of threads. */
    int maxThreads{10};
    /*! This is the minimum stack size allocated for each thread. */
    size_t stackSize{0};
    /*! This is the maximum time a thread will
     * remain idle before dying (in milliseconds). */
    int maxIdleTime{10 * 1000};
    /*! Jobs per thread to maintain. */
    int jobsPerThread{10};
    /*! Maximum number of jobs that can be queued totally. */
    int maxJobsTotal{500};
    /*! the time a low priority or med priority job waits before getting
     * bumped up a priority (in milliseconds). */
    int starvationTime{500};
    /*! scheduling policy to use. */
    PolicyType schedPolicy{SCHED_OTHER};
};


/*! Structure to hold statistics. */
struct ThreadPoolStats {
    double totalTimeHQ{0};
    int totalJobsHQ{0};
    double avgWaitHQ{0};
    double totalTimeMQ{0};
    int totalJobsMQ{0};
    double avgWaitMQ{0};
    double totalTimeLQ{0};
    int totalJobsLQ{0};
    double avgWaitLQ{0};
    double totalWorkTime{0};
    double totalIdleTime{0};
    int workerThreads{0};
    int idleThreads{0};
    int persistentThreads{0};
    int totalThreads{0};
    int maxThreads{0};
    int currentJobsHQ{0};
    int currentJobsLQ{0};
    int currentJobsMQ{0};
};

/*!
 * \brief A thread pool similar to the thread pool in the UPnP SDK.
 *
 * Allows jobs to be scheduled for running by threads in a 
 * thread pool. The thread pool is initialized with a 
 * minimum and maximum thread number as well as a max idle time
 * and a jobs per thread ratio. If a worker thread waits the whole
 * max idle time without receiving a job and the thread pool
 * currently has more threads running than the minimum
 * then the worker thread will exit. If when 
 * scheduling a job the current job to thread ratio
 * becomes greater than the set ratio and the thread pool currently has
 * less than the maximum threads then a new thread will
 * be created.
 */
class ThreadPool {
public:
    enum ThreadPriority {LOW_PRIORITY, MED_PRIORITY, HIGH_PRIORITY};
    /* Function for freeing a thread argument. */
    typedef void (*free_routine)(void *arg);

    ThreadPool();
#if 0
    ~ThreadPool();
#endif
    /* Initialize things and start up returns 0 if ok */
    int start(ThreadPoolAttr *attr = nullptr);

    /* Add regular job. To be scheduled asap, we don't wait for it to start */
    int addJob(start_routine func,
               void *arg = nullptr, free_routine free_func = nullptr, 
               ThreadPriority priority = MED_PRIORITY);

    /*!
     * \brief Adds a persistent job to the thread pool.
     * Job will be run as soon as possible. Call will block until job
     * is scheduled.
     *
     * \return
     *    \li \c 0 on success.
     *    \li \c EOUTOFMEM not enough memory to add job.
     *    \li \c EMAXTHREADS not enough threads to add persistent job.
     */
    int addPersistent(start_routine func, void *arg = nullptr, 
                      free_routine free_func = nullptr,
                      ThreadPriority priority = MED_PRIORITY);

    /*!
     * \brief Gets the current set of attributes associated with the
     * thread pool.
     *
     * \return
     *     \li \c 0 on success, nonzero on failure.
     */
    int getAttr(ThreadPoolAttr *out);

    /*!
     * \brief Sets the attributes for the thread pool.
     * Only affects future calculations.
     *
     * \return
     *     \li \c 0 on success, nonzero on failure.
     *     \li \c INVALID_POLICY if policy can not be set.
     */
    int setAttr(ThreadPoolAttr *attr);

    /*!
     * \brief Shuts the thread pool down. Waits for all threads to finish.
     * May block indefinitely if jobs do not exit.
     *
     * \return 0 on success, nonzero on failure
     */
    int shutdown();

    /*!
     * \brief Returns various statistics about the thread pool.
     *
     * \return Always returns 0.
     */
    int getStats(ThreadPoolStats *stats);
    void printStats(ThreadPoolStats *stats);

    class Internal;
private:
    Internal *m;
};

#endif /* THREADPOOL_H */

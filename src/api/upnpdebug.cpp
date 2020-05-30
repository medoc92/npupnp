/*******************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation 
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

/*!
 * \file
 */

#include "config.h"

#include "upnp.h"
#include "upnpdebug.h"

#include <mutex>
#include <thread>
#include <sstream>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef DEBUG

/*! Mutex to synchronize all the log file operations in the debug mode */
static std::mutex GlobalDebugMutex;

/*! Global log level */
static Upnp_LogLevel g_log_level = UPNP_DEFAULT_LOG_LEVEL;

/* Output file pointer */
static FILE *fp;
static int is_stderr;

/* Set if the user called setlogfilename() or setloglevel() */
static int setlogwascalled;
/* Name of the output file. We keep a copy */
static char *fileName;

/* This is called from UpnpInit(). So the user must call setLogFileName() 
 * before. This can be called again, for example to rotate the log
 * file, and we try to avoid multiple calls to the mutex init, with a
 * risk of race, probably not a problem, and not worth fixing. */
int UpnpInitLog(void)
{
    /* If the user did not ask for logging do nothing */
    if (setlogwascalled == 0 && !getenv("NPUPNP_FORCELOG")) {
        return UPNP_E_SUCCESS;
    }
    if (fp) {
        if (is_stderr == 0) {
            fclose(fp);
            fp = nullptr;
        }
    }
    is_stderr = 0;
    if (fileName) {
        if ((fp = fopen(fileName, "a")) == nullptr) {
            fprintf(stderr, "Failed to open fileName (%s): %s\n",
                    fileName, strerror(errno));
        }
    }
    if (fp == nullptr) {
        fp = stderr;
        is_stderr = 1;
    }
    return UPNP_E_SUCCESS;
}

void UpnpSetLogLevel(Upnp_LogLevel log_level)
{
    g_log_level = log_level;
    setlogwascalled = 1;
}

void UpnpCloseLog(void)
{
    /* Calling lock() assumes that someone called UpnpInitLog(), but
     * this is reasonable as it is called from UpnpInit(). We risk a
     * crash if we do this without a lock.*/
    std::unique_lock<std::mutex> lck(GlobalDebugMutex);

    if (fp != nullptr && is_stderr == 0) {
        fclose(fp);
    }
    fp = nullptr;
    is_stderr = 0;
}

void UpnpSetLogFileNames(const char *newFileName, const char *ignored)
{
    (void)ignored;

    if (fileName) {
        free(fileName);
        fileName = nullptr;
    }
    if (newFileName && *newFileName) {
        fileName = strdup(newFileName);
    }
    setlogwascalled = 1;
}

static int DebugAtThisLevel(Upnp_LogLevel DLevel, Dbg_Module Module)
{
    (void)Module;

    return (DLevel <= g_log_level) &&
        (DEBUG_ALL ||
         (Module == SSDP && DEBUG_SSDP) ||
         (Module == SOAP && DEBUG_SOAP) ||
         (Module == GENA && DEBUG_GENA) ||
         (Module == TPOOL && DEBUG_TPOOL) ||
         (Module == MSERV && DEBUG_MSERV) ||
         (Module == DOM && DEBUG_DOM) || (Module == HTTP && DEBUG_HTTP));
}

static void UpnpDisplayFileAndLine(
    FILE *fp, const char *DbgFileName,
    int DbgLineNo, Upnp_LogLevel DLevel, Dbg_Module Module)
{
    char timebuf[26];
    time_t now = time(nullptr);
    struct tm *timeinfo;
    const char *smod;
#if 0
    char *slev;
    /* Code kept around in case, but I think it's actually more convenient
       to display a numeric level */
    switch (DLevel) {
    case UPNP_CRITICAL: slev="CRI";break;
    case UPNP_ERROR: slev="ERR";break;
    case UPNP_INFO: slev="INF";break;
    case UPNP_ALL: slev="ALL";break;
    default: slev="UNK";break;
    }
#else
    char slev[25];
    snprintf(slev, 25, "%d", DLevel);
#endif
        
    switch(Module) {
    case SSDP: smod="SSDP";break;
    case SOAP: smod="SOAP";break;
    case GENA: smod="GENA";break;
    case TPOOL: smod="TPOL";break;
    case MSERV: smod="MSER";break;
    case DOM: smod="DOM_";break;
    case API: smod="API_";break;
    case HTTP: smod="HTTP";break;
    default: smod="UNKN";break;
    }

    timeinfo = localtime(&now);
    strftime(timebuf, 26, "%Y-%m-%d %H:%M:%S", timeinfo);
    std::ostringstream ss;
    ss << "0x" << std::hex << std::this_thread::get_id();
    fprintf(fp, "%s UPNP-%s-%s: Thread:%s [%s:%d]: ", timebuf, smod, slev,
            ss.str().c_str(), DbgFileName, DbgLineNo);
    fflush(fp);
}

void UpnpPrintf(
    Upnp_LogLevel DLevel, Dbg_Module Module,
    const char *DbgFileName, int DbgLineNo, const char *FmtStr, ...)
{
    /*fprintf(stderr, "UpnpPrintf: fp %p level %d glev %d mod %d DEBUG_ALL %d\n",
      fp, DLevel, g_log_level, Module, DEBUG_ALL);*/
    va_list ArgList;

    if (!DebugAtThisLevel(DLevel, Module))
        return;

    std::unique_lock<std::mutex> lck(GlobalDebugMutex);
    if (fp == nullptr) {
        return;
    }

    va_start(ArgList, FmtStr);
    if (DbgFileName) {
        UpnpDisplayFileAndLine(fp, DbgFileName, DbgLineNo, DLevel, Module);
        vfprintf(fp, FmtStr, ArgList);
        fflush(fp);
    }
    va_end(ArgList);
}

/* No locking here, the app should be careful about not calling
   closelog from a separate thread... */
FILE *UpnpGetDebugFile(Upnp_LogLevel DLevel, Dbg_Module Module)
{
    if (!DebugAtThisLevel(DLevel, Module)) {
        return    nullptr;
    }

    return fp;
}


#endif /* DEBUG */

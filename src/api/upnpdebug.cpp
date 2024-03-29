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

#include "config.h"

#include "upnp.h"
#include "upnpdebug.h"

#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

/* Mutex to synchronize all the log file operations in the debug mode */
static std::mutex GlobalDebugMutex;

/* Global log level */
static Upnp_LogLevel g_log_level = UPNP_DEFAULT_LOG_LEVEL;

/* Output file pointer */
static FILE *fp;
static int is_stderr;

/* Set if the user called setlogfilename() or setloglevel() */
static int setlogwascalled;
/* Name of the output file. We keep a copy */
static std::string fileName;

/* This can be called multiple times, for example to rotate the log file.*/
int UpnpInitLog(void)
{
    if (setlogwascalled == 0) {
        const char* envlevel = getenv("NPUPNP_LOGLEVEL");
        const char* envfn = getenv("NPUPNP_LOGFILENAME");
        /* Maybe a call from UpnpInit(). If the user did not ask for
           logging do nothing, else init from environment or filename
           and level from the api calls */
        if (nullptr == envlevel && nullptr == envfn) {
            return UPNP_E_SUCCESS;
        }
        if (envlevel) {
            g_log_level = static_cast<Upnp_LogLevel>(atoi(envlevel));
        }
        if (envfn) {
            fileName = envfn;
        }
    }
    if (fp && !is_stderr) {
        fclose(fp);
        fp = nullptr;
        is_stderr = 0;
    }
    if (!fileName.empty()) {
        if ((fp = fopen(fileName.c_str(), "a")) == nullptr) {
            std::cerr<<"UpnpDebug: failed to open ["<< fileName << "] : " << strerror(errno) << "\n";
        }
        is_stderr = 0;
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
    std::scoped_lock lck(GlobalDebugMutex);

    if (fp != nullptr && is_stderr == 0) {
        fclose(fp);
    }
    fp = nullptr;
    is_stderr = 0;
}

void UpnpSetLogFileNames(const char *newFileName, const char *ignored)
{
    (void)ignored;

    fileName.clear();
    if (newFileName && *newFileName) {
        fileName = newFileName;
    }
    setlogwascalled = 1;
}

static int DebugAtThisLevel(Upnp_LogLevel DLevel, Dbg_Module Module)
{
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
    const struct tm *timeinfo;
    const char *smod;

    auto slev = std::to_string(DLevel);
        
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
    fprintf(fp, "%s UPNP-%s-%s: Thread:%s [%s:%d]: ", timebuf, smod, slev.c_str(),
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

    std::scoped_lock lck(GlobalDebugMutex);
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
        return nullptr;
    }
    return fp;
}

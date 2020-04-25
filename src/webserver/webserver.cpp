/**************************************************************************
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
 **************************************************************************/

/*!
 * \file
 *
 * \brief General purpose web server (non-soap/gena/ssdp requests)
 */

#include "config.h"

#if EXCLUDE_WEB_SERVER == 0

#include "webserver.h"

#include <map>
#include <iostream>
#include <algorithm>
#include <mutex>
#include <condition_variable>

#include <inttypes.h>

#include "httputils.h"
#include "ssdplib.h"
#include "statcodes.h"
#include "upnp.h"
#include "upnpapi.h"
#include "VirtualDir.h"
#include "smallut.h"

#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>

/*!
 * Response Types.
 */
enum resp_type {
	RESP_FILEDOC,
	RESP_HEADERS,
	RESP_WEBDOC,
	RESP_XMLDOC,
};

struct SendInstruction {
	std::string AcceptLanguageHeader;
	/* Offset to begin reading at. Set by range header if present */
	int64_t offset{0};
	/*! Requested read size. -1 -> end of resource. Set by range
	    header if present */
	int64_t ReadSendSize{-1};
	/*! Cookie associated with the virtualDir. This is set if the path
	    matches a VirtualDir entry, and is passed to subsequent
	    virtual dir calls. */
	const void *cookie{nullptr};
	/* Copy of the data if this request is for a localdoc, e.g. the
	   description document set by the API if we serve it this way,
	   instead of as a local file or a virtualdir entry (this depends
	   on the kind of registerrootdevice call) */
	std::string data;
	/* This is set by the Virtual Dir GetInfo user callback and passed to 
	   further VirtualDirectory calls for the same request */
	const void* request_cookie{nullptr};
};

/*!
 * module variables - Globals, static and externs.
 */

static const std::map<std::string, const char*> gEncodedMediaTypes = {
	{"aif", "audio/aiff"},
	{"aifc", "audio/aiff"},
	{"aiff", "audio/aiff"},
	{"asf", "video/x-ms-asf"},
	{"asx", "video/x-ms-asf"},
	{"au", "audio/basic"},
	{"avi", "video/msvideo"},
	{"bmp", "image/bmp"},
	{"css", "text/css"},
	{"dcr", "application/x-director"},
	{"dib", "image/bmp"},
	{"dir", "application/x-director"},
	{"dxr", "application/x-director"},
	{"gif", "image/gif"},
	{"hta", "text/hta"},
	{"htm", "text/html"},
	{"html", "text/html"},
	{"jar", "application/java-archive"},
	{"jfif", "image/pjpeg"},
	{"jpe", "image/jpeg"},
	{"jpeg", "image/jpeg"},
	{"jpg", "image/jpeg"},
	{"js", "application/x-javascript"},
	{"kar", "audio/midi"},
	{"m3u", "audio/mpegurl"},
	{"mid", "audio/midi"},
	{"midi", "audio/midi"},
	{"mov", "video/quicktime"},
	{"mp2v", "video/x-mpeg2"},
	{"mp3", "audio/mpeg"},
	{"mpe", "video/mpeg"},
	{"mpeg", "video/mpeg"},
	{"mpg", "video/mpeg"},
	{"mpv", "video/mpeg"},
	{"mpv2", "video/x-mpeg2"},
	{"pdf", "application/pdf"},
	{"pjp", "image/jpeg"},
	{"pjpeg", "image/jpeg"},
	{"plg", "text/html"},
	{"pls", "audio/scpls"},
	{"png", "image/png"},
	{"qt", "video/quicktime"},
	{"ram", "audio/x-pn-realaudio"},
	{"rmi", "audio/mid"},
	{"rmm", "audio/x-pn-realaudio"},
	{"rtf", "application/rtf"},
	{"shtml", "text/html"},
	{"smf", "audio/midi"},
	{"snd", "audio/basic"},
	{"spl", "application/futuresplash"},
	{"ssm", "application/streamingmedia"},
	{"swf", "application/x-shockwave-flash"},
	{"tar", "application/tar"},
	{"tcl", "application/x-tcl"},
	{"text", "text/plain"},
	{"tif", "image/tiff"},
	{"tiff", "image/tiff"},
	{"txt", "text/plain"},
	{"ulw", "audio/basic"},
	{"wav", "audio/wav"},
	{"wax", "audio/x-ms-wax"},
	{"wm", "video/x-ms-wm"},
	{"wma", "audio/x-ms-wma"},
	{"wmv", "video/x-ms-wmv"},
	{"wvx", "video/x-ms-wvx"},
	{"xbm", "image/x-xbitmap"},
	{"xml", "text/xml"},
	{"xsl", "text/xml"},
	{"z", "application/x-compress"},
	{"zip", "application/zip"}
};


/*! Global variable. A file system directory which serves as webserver
    root. If this is not set from the API UpnpSetWebServerRootDir()
    call, we do not serve files from the file system at all (only
    possibly the virtual dir and/or the localDocs). */
static std::string gDocumentRootDir;

struct LocalDoc {
	std::string data;
	time_t last_modified;
};

// Data which we serve directly: usually description
// documents. Indexed by path. Content-Type is always text/xml
// The map is tested after the virtualdir, so the latter has priority.
static std::map<std::string, LocalDoc> localDocs;

static std::mutex gWebMutex;

class VirtualDirListEntry {
public:
	std::string path;
	const void *cookie;
};
/* Virtual directory list. This is used with a linear search by the
   web server to perform prefix matches. */
static std::vector<VirtualDirListEntry> virtualDirList;

/* Compute MIME type from file name extension. */
static UPNP_INLINE int get_content_type(
	const char *filename, std::string& content_type)
{
	const char *ctname = "application/octet-stream";
	content_type.clear();
	/* get ext */
	const char *e = strrchr(filename, '.');
	if (e) {
		e++;
		std::string le = stringtolower(e);
		auto it = gEncodedMediaTypes.find(le);
		if (it != gEncodedMediaTypes.end()) {
			ctname = it->second;
		}
	}
	content_type = ctname;
	return 0;
}

int web_server_set_localdoc(
	const std::string& path, const std::string& data, time_t last_modified)
{
	if (path.empty() || path.front() != '/') {
		return UPNP_E_INVALID_PARAM;
	}
	LocalDoc doc;
	doc.data = data;
	doc.last_modified = last_modified;
	std::unique_lock<std::mutex> lck(gWebMutex);
	localDocs[path] = doc;
	return UPNP_E_SUCCESS;
}

int web_server_unset_localdoc(const std::string& path)
{
	std::unique_lock<std::mutex> lck(gWebMutex);
	auto it = localDocs.find(path);
	if (it != localDocs.end())
		localDocs.erase(it);
	return UPNP_E_SUCCESS;
}

int web_server_init()
{
	if (bWebServerState == WEB_SERVER_DISABLED) {
		virtualDirCallback.get_info = nullptr;
		virtualDirCallback.open = nullptr;
		virtualDirCallback.read = nullptr;
		virtualDirCallback.write = nullptr;
		virtualDirCallback.seek = nullptr;
		virtualDirCallback.close = nullptr;
		bWebServerState = WEB_SERVER_ENABLED;
	}
	return 0;
}

/*!
 * \brief Release memory allocated for the global web server root directory
 * and the global XML document. Resets the flag bWebServerState to
 * WEB_SERVER_DISABLED.
 *
 */
void web_server_destroy()
{
	if (bWebServerState == WEB_SERVER_ENABLED) {
		gDocumentRootDir.clear();
		localDocs.clear();
		bWebServerState = WEB_SERVER_DISABLED;
	}
}

/* Get file information, local file system version */
static int get_file_info(const char *filename, struct File_Info *info)
{
	info->content_type.clear();
	struct stat s;
	if (stat(filename, &s) == -1)
		return -1;
	if (S_ISDIR(s.st_mode))
		info->is_directory = true;
	else if (S_ISREG(s.st_mode))
		info->is_directory = false;
	else
		return -1;
	/* check readable */
	FILE *fp = fopen(filename, "r");
	info->is_readable = (fp != nullptr);
	if (fp)
		fclose(fp);
	info->file_length = s.st_size;
	info->last_modified = s.st_mtime;
	int rc = get_content_type(filename, info->content_type);
	UpnpPrintf(UPNP_INFO, HTTP, __FILE__, __LINE__,
			   "get_file_info: %s, sz: %" PRIi64 ", mtime=%s rdable=%d\n",
			   filename, (int64_t)info->file_length,
			   make_date_string(info->last_modified).c_str(),
			   info->is_readable);

	return rc;
}

int web_server_set_root_dir(const char *root_dir)
{
	gDocumentRootDir = root_dir;
	/* remove trailing '/', if any */
	if (!gDocumentRootDir.empty() && gDocumentRootDir.back() == '/') {
		gDocumentRootDir.pop_back();
	}

	return 0;
}

int web_server_add_virtual_dir(
	const char *dirname, const void *cookie, const void **oldcookie)
{
	if (!dirname || !*dirname) {
		return UPNP_E_INVALID_PARAM;
	}

	UpnpPrintf(UPNP_DEBUG, HTTP, __FILE__, __LINE__,
			   "web_server_add_virtual_dir: [%s]\n", dirname);

	VirtualDirListEntry entry;
	entry.cookie = cookie;
	if (dirname[0] != '/') {
		// Gerbera does this ?? I think that it should be illegal,
		// esp. since it then proceeds to use the absolute path for
		// requests. Not sure why it works with libupnp
		entry.path = std::string("/") + dirname;
	} else {
		entry.path = dirname;
	}
    if (entry.path.back() != '/') {
        entry.path += '/';
    }

	auto old = std::find_if(virtualDirList.begin(), virtualDirList.end(),
							[entry](const VirtualDirListEntry& old) {
								return entry.path == old.path;
							});
	if (old != virtualDirList.end()) {
		if (oldcookie) {
			*oldcookie = old->cookie;
		}
		*old = entry;
	} else {
		virtualDirList.push_back(entry);
	}
	return UPNP_E_SUCCESS;
}

int web_server_remove_virtual_dir(const char *dirname)
{
	if (dirname == nullptr) {
		return UPNP_E_INVALID_PARAM;
	}
	for (auto it = virtualDirList.begin(); it != virtualDirList.end(); it++) {
		if (it->path == dirname) {
			virtualDirList.erase(it);
			return UPNP_E_SUCCESS;
		}
	}

	return UPNP_E_INVALID_PARAM;
}

void web_server_clear_virtual_dirs()
{
	virtualDirList.clear();
}

/*!
 * \brief Compares filePath with paths from the list of virtual directory
 * lists.
 *
 * \return nullptr or entry pointer.
 */
static const VirtualDirListEntry *isFileInVirtualDir(const std::string& path)
{
	for (const auto& vd : virtualDirList) {
        // We ensure that vd entries paths end with /. Meaning that if
        // the paths compare equal up to the vd path len, the input
        // path is in a subdir of the vd path.
        if (!vd.path.compare(0, vd.path.size(), path, 0, vd.path.size())) {
            return &vd;
		}
    } 
	return nullptr;
}

/* Parse a Range header */
static bool parseRanges(
	const std::string& ranges, std::vector<std::pair<int64_t, int64_t>>& oranges)
{
    oranges.clear();
	std::string::size_type pos = ranges.find("bytes=");
    if (pos == std::string::npos) {
        return false;
    }
    pos += 6;
    bool done = false;
    while(!done) {
		std::string::size_type dash = ranges.find('-', pos);
        if (dash == std::string::npos) {
            return false;
        }
        std::string::size_type comma = ranges.find(',', pos);
        std::string firstPart = ranges.substr(pos, dash-pos);
        int64_t start = firstPart.empty() ? 0 : atoll(firstPart.c_str());
        std::string secondPart = ranges.substr(
			dash+1, comma != std::string::npos ?
			comma-dash-1 : std::string::npos);
        int64_t fin = secondPart.empty() ? -1 : atoll(secondPart.c_str());
		std::pair<int64_t, int64_t> nrange(start,fin);
        oranges.push_back(nrange);
        if (comma != std::string::npos) {
            pos = comma + 1;
        }
        done = comma == std::string::npos;
    }
    return true;
}

/*!
 * \brief Other header processing. Only HDR_ACCEPT_LANGUAGE for now.
 */
static int CheckOtherHTTPHeaders(
	MHDTransaction *mhdt, struct SendInstruction *RespInstr, int64_t filesize)
{
	for (const auto& header : mhdt->headers) {
		/* find header type. */
		int index = httpheader_str2int(header.first);
		const std::string& hvalue = header.second;
		if (index >= 0) {
			switch (index) {
			case HDR_ACCEPT_LANGUAGE:
				RespInstr->AcceptLanguageHeader = hvalue;
				break;
			default:
				/*	TODO? */
				break;
			}
		}
	}

	return HTTP_OK;
}

/*!
 * \brief Processes the request and returns the result in the output parameters.
 *
 * \return HTTP status code.
 */
static int process_request(
	MHDTransaction *mhdt,
	/*! [out] Type of response: what source type the data will come from. */
	enum resp_type *rtype,
	/*! [out] Headers for the response. */
	std::map<std::string, std::string>& headers,
	/*! [out] Computed actual file path. */
	std::string& filename,
	/*! [out] Send Instruction object where the response is set up. */
	struct SendInstruction *RespInstr)
{
	struct File_Info finfo;
	LocalDoc localdoc;
	
	assert(mhdt->method == HTTPMETHOD_GET ||
		   mhdt->method == HTTPMETHOD_HEAD ||
		   mhdt->method == HTTPMETHOD_POST ||
		   mhdt->method == HTTPMETHOD_SIMPLEGET);

	if (mhdt->method == HTTPMETHOD_POST) {
		return HTTP_FORBIDDEN;
	}

	std::vector<std::pair<int64_t, int64_t> > ranges;
	auto it = mhdt->headers.find("range");
    if (it != mhdt->headers.end()) {
		if (parseRanges(it->second, ranges) && !ranges.empty()) {
			if (ranges.size() > 1) {
				return HTTP_REQUEST_RANGE_NOT_SATISFIABLE;
			}

			RespInstr->offset = ranges[0].first;
			if (ranges[0].second >= 0) {
				RespInstr->ReadSendSize = ranges[0].second -
					ranges[0].first + 1;
				if (RespInstr->ReadSendSize < 0) {
					RespInstr->ReadSendSize = 0;
				}
			} else {
				RespInstr->ReadSendSize = -1;
			}
		}
	}

	/* init */
	const VirtualDirListEntry *entryp{nullptr};

	/* Data we supply as input to the file info gathering functions */
	finfo.request_headers = mhdt->headers;
	mhdt->copyClientAddress(&finfo.CtrlPtIPAddr);
	mhdt->copyHeader("user-agent", finfo.Os);

	/* Unescape and canonize the path. Note that MHD has already
       stripped a possible query part ("?param=value...)  for us */
    std::string request_doc = remove_escaped_chars(mhdt->url);
	request_doc = remove_dots(request_doc);
	if (request_doc.empty()) {
		return HTTP_FORBIDDEN;
	}
	if (request_doc[0] != '/') {
		/* no slash */
		return HTTP_BAD_REQUEST;
	}
	entryp = isFileInVirtualDir(request_doc);
	if (!entryp) {
		std::unique_lock<std::mutex> lck(gWebMutex);
		auto localdocit = localDocs.find(request_doc);
		// Just make a copy. Could do better using a
		// map<string,share_ptr> like the original, but I don't think
		// that the perf impact is significant
		if (localdocit != localDocs.end()) {
			localdoc = localdocit->second;
		}
	}
	if (entryp) {
		*rtype = RESP_WEBDOC;
		RespInstr->cookie = entryp->cookie;
		filename = request_doc;
		std::string qs;
		if (!mhdt->queryvalues.empty()) {
			qs = "?";
			for (const auto& entry : mhdt->queryvalues) {
				qs += query_encode(entry.first) + "=" +
					query_encode(entry.second);
				qs += "&";
			}
			qs.pop_back();
		}
		std::string bfilename{filename};
		filename += qs;
		/* get file info */
		if (virtualDirCallback.get_info(
				filename.c_str(), &finfo, entryp->cookie,
				&RespInstr->request_cookie) != UPNP_E_SUCCESS) {
			return HTTP_NOT_FOUND;
		}
		/* try index.html if req is a dir */
		if (finfo.is_directory) {
			const char *temp_str;
			if (bfilename.back() == '/') {
				temp_str = "index.html";
			} else {
				temp_str = "/index.html";
			}
			bfilename += temp_str;
			filename = bfilename + qs;
			/* get info */
			if ((virtualDirCallback.get_info(
					 filename.c_str(), &finfo, entryp->cookie,
					 &RespInstr->request_cookie) != UPNP_E_SUCCESS) ||
				finfo.is_directory) {
				return HTTP_NOT_FOUND;
			}
		}
		/* not readable */
		if (!finfo.is_readable) {
			return HTTP_FORBIDDEN;
		}
	} else if (!localdoc.data.empty()) {
		*rtype = RESP_XMLDOC;
		finfo.content_type = "text/xml";
		finfo.file_length = localdoc.data.size();
		finfo.is_readable = true;
		finfo.is_directory = false;
		finfo.last_modified = localdoc.last_modified;
		RespInstr->data.swap(localdoc.data);
	} else {
		*rtype = RESP_FILEDOC;
		if (gDocumentRootDir.empty()) {
			return HTTP_FORBIDDEN;
		}
		/* get file name */
		filename = gDocumentRootDir;
		filename += request_doc;
		/* remove trailing slashes */
		while (!filename.empty() && filename.back() == '/') {
			filename.pop_back();
		}

		/* get info on file */
		if (get_file_info(filename.c_str(), &finfo) != 0) {
			return HTTP_NOT_FOUND;
		}
		/* try index.html if req is a dir */
		if (finfo.is_directory) {
			const char *temp_str;
			if (filename.back() == '/') {
				temp_str = "index.html";
			} else {
				temp_str = "/index.html";
			}
			filename += temp_str;
			/* get info */
			if (get_file_info(filename.c_str(), &finfo) != 0 ||
				finfo.is_directory) {
				return HTTP_NOT_FOUND;
			}
		}
		/* not readable */
		if (!finfo.is_readable) {
			return HTTP_FORBIDDEN;
		}
	}

	if (RespInstr->ReadSendSize < 0) {
		// No range specified or open-ended range
		RespInstr->ReadSendSize = finfo.file_length - RespInstr->offset;
	} else if (RespInstr->offset + RespInstr->ReadSendSize > finfo.file_length) {
		RespInstr->ReadSendSize = finfo.file_length - RespInstr->offset;
	}
	
	//std::cerr << "process_request: offset " << RespInstr->offset <<
	// " count " << RespInstr->ReadSendSize << "\n";
		
	int code = CheckOtherHTTPHeaders(mhdt, RespInstr, finfo.file_length);
	if (code != HTTP_OK) {
		return code;
	}

	/* simple get http 0.9 as specified in http 1.0 */
	/* don't send headers */
	if (mhdt->method == HTTPMETHOD_SIMPLEGET) {
		headers.clear();
		return HTTP_OK;
	}

	// Add any headers created by the client in the GetInfo callback
	headers.insert(finfo.response_headers.begin(), finfo.response_headers.end());

	if (!finfo.content_type.empty()) {
		headers["content-type"] = finfo.content_type;
	}
	if (RespInstr->AcceptLanguageHeader[0] && WEB_SERVER_CONTENT_LANGUAGE[0]) {
		headers["content-language"] = WEB_SERVER_CONTENT_LANGUAGE;
	}
	{
		std::string date = make_date_string(0);
		if (!date.empty())	
			headers["date"] = date;
		if (finfo.last_modified) {
			headers["last-modified"] = make_date_string(finfo.last_modified);
		}
	}
	headers["x-user-agent"] = X_USER_AGENT;

	if (mhdt->method == HTTPMETHOD_HEAD) {
		*rtype = RESP_HEADERS;
	} 

	return HTTP_OK;
}

class VFileReaderCtxt {
public:
	~VFileReaderCtxt() = default;
	UpnpWebFileHandle fp{nullptr};
	const void *cookie;
	const void *request_cookie;
};

static ssize_t vFileReaderCallback(void *cls, uint64_t pos, char *buf,
								   size_t max)
{
	auto ctx = (VFileReaderCtxt*)cls;
	if (nullptr == ctx->fp) {
		UpnpPrintf(UPNP_ERROR, MSERV, __FILE__, __LINE__,
				   "vFileReaderCallback: fp is null !\n");
		return -1;
	}
	//std::cerr << "vFileReaderCallback: pos " << pos << " cnt " << max << "\n";
#if LOCKS_UP_GERBERA_DONT_DO_IT
	if (virtualDirCallback.seek(
			ctx->fp, pos, SEEK_SET, ctx->cookie, ctx->request_cookie) !=
		(int64_t)pos) {
		return -1;
	}
#endif
	int ret = virtualDirCallback.read(
		ctx->fp, buf, max, ctx->cookie, ctx->request_cookie);

	/* From the microhttpd manual: Note that returning zero will cause
	   MHD to try again. Thus, returning zero should only be used in
	   conjunction with MHD_suspend_connection() to avoid busy
	   waiting. */
	return ret <= 0 ? -1 : ret;
}

static void vFileFreeCallback (void *cls)
{
	if (cls) {
		auto ctx = (VFileReaderCtxt*)cls;
		virtualDirCallback.close(ctx->fp, ctx->cookie, ctx->request_cookie);
		delete ctx;
	}
}

void web_server_callback(MHDTransaction *mhdt)
{
	int ret;
	auto rtype = (enum resp_type)0;
	std::map<std::string,std::string> headers;
	std::string filename;
	struct SendInstruction RespInstr;

	/* Process request should create the different kind of header depending 
	   on the the type of request. */
	ret = process_request(mhdt, &rtype, headers, filename, &RespInstr);
	if (ret != HTTP_OK) {
		/* send error code */
		http_SendStatusResponse(mhdt, ret);
	} else {
		/* send response */
		switch (rtype) {
		case RESP_FILEDOC:
		{
			int fd = open(filename.c_str(), 0);
			if (fd < 0) {
				http_SendStatusResponse(mhdt, HTTP_FORBIDDEN);
			} else {
				mhdt->response = MHD_create_response_from_fd_at_offset64(
					RespInstr.ReadSendSize, fd, RespInstr.offset);
				mhdt->httpstatus = 200;
			}
		}
		break;

		case RESP_WEBDOC:
		{
			auto ctx = new VFileReaderCtxt;
			ctx->fp = virtualDirCallback.open(
				filename.c_str(), UPNP_READ,
				RespInstr.cookie,	RespInstr.request_cookie);
			if (ctx->fp == nullptr) {
				http_SendStatusResponse(mhdt, HTTP_INTERNAL_SERVER_ERROR);
			}
			ctx->cookie = RespInstr.cookie;
			ctx->request_cookie = RespInstr.request_cookie;
			if (RespInstr.offset) {
				virtualDirCallback.seek(ctx->fp, RespInstr.offset, SEEK_SET,
										ctx->cookie, ctx->request_cookie);
			}
			mhdt->response = MHD_create_response_from_callback(
				RespInstr.ReadSendSize, 4096, vFileReaderCallback,
				ctx, vFileFreeCallback);
			mhdt->httpstatus = 200;
		}
		break;

		case RESP_XMLDOC:
			mhdt->response = MHD_create_response_from_buffer(
				RespInstr.data.size(), (void*)(strdup(RespInstr.data.c_str())),
				MHD_RESPMEM_PERSISTENT);
			mhdt->httpstatus = 200;
			break;

		case RESP_HEADERS:
			/* headers only */
			mhdt->response = MHD_create_response_from_buffer(
				0, nullptr, MHD_RESPMEM_PERSISTENT);
			break;

		default:
			UpnpPrintf(UPNP_INFO, HTTP, __FILE__, __LINE__,
					   "webserver: Generated an invalid response type.\n");
			assert(0);
		}
	}
	for (const auto& header : headers) {
		//std::cerr << "web_server_callback: adding header [" << header.first <<
		//"]->[" << header.second << "]\n";
		MHD_add_response_header(mhdt->response, header.first.c_str(),
								header.second.c_str());
	}
	MHD_add_response_header(mhdt->response, "Accept-Ranges", "bytes");
	
	UpnpPrintf(UPNP_INFO,HTTP,__FILE__,__LINE__, "webserver: response ready.\n");
}
#endif /* EXCLUDE_WEB_SERVER */

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
	/*! Read from local source and send on the network. */
	int64_t ReadSendSize{0};
	/*! Cookie associated with the virtualDir. */
	const void *cookie{nullptr};
	std::string data;
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


/*! Global variable. A local dir which serves as webserver root. */
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
		virtualDirCallback.get_info = NULL;
		virtualDirCallback.open = NULL;
		virtualDirCallback.read = NULL;
		virtualDirCallback.write = NULL;
		virtualDirCallback.seek = NULL;
		virtualDirCallback.close = NULL;
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
void web_server_destroy(void)
{
	if (bWebServerState == WEB_SERVER_ENABLED) {
		gDocumentRootDir.clear();
		localDocs.clear();
		bWebServerState = WEB_SERVER_DISABLED;
	}
}

static int get_file_info(
	/*! [in] Filename having the description document. */
	const char *filename,
	/*! [out] File information object having file attributes such as filelength,
	 * when was the file last modified, whether a file or a directory and
	 * whether the file or directory is readable. */
	struct File_Info *info)
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
	info->is_readable = (fp != NULL);
	if (fp)
		fclose(fp);
	info->file_length = s.st_size;
	info->last_modified = s.st_mtime;
	int rc = get_content_type(filename, info->content_type);
	UpnpPrintf(UPNP_INFO, HTTP, __FILE__, __LINE__,
			   "file info: %s, length: %lld, last_mod=%s readable=%d\n",
			   filename, (long long)info->file_length,
			   make_date_string(info->last_modified).c_str(),
			   info->is_readable);

	return rc;
}

int web_server_set_root_dir(const char *root_dir)
{
	gDocumentRootDir = root_dir;
	/* remove trailing '/', if any */
	if (gDocumentRootDir.size() > 0 && gDocumentRootDir.back() == '/') {
		gDocumentRootDir.pop_back();
	}

	return 0;
}

int web_server_add_virtual_dir(
	const char *dirname, const void *cookie, const void **oldcookie)
{
	if (!dirname || !*dirname || dirname[0] != '/') {
		return UPNP_E_INVALID_PARAM;
	}

	VirtualDirListEntry entry;
	entry.cookie = cookie;
    entry.path = dirname;
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
	if (dirname == NULL) {
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


/*!
 * \brief Get header id from the request parameter and take appropriate
 * action based on the ids.
 *
 * \return
 * \li \c HTTP_BAD_REQUEST
 * \li \c HTTP_INTERNAL_SERVER_ERROR
 * \li \c HTTP_REQUEST_RANGE_NOT_SATISFIABLE
 * \li \c HTTP_OK
 */
static int CheckOtherHTTPHeaders(
	MHDTransaction *mhdt, struct SendInstruction *RespInstr, off_t FileSize)
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

#ifdef EXTRA_HEADERS_AS_LIST
/*!
 * \brief Build an array of unrecognized headers.
 *
 * \return nothing
 */
#define MAX_EXTRA_HEADERS 128
static int ExtraHTTPHeaders(MHDTransaction *mhdt,
							struct Extra_Headers **ExtraHeaders)
{
	int index, nb_extra = 0;
	struct Extra_Headers *extra_headers;

	extra_headers = *ExtraHeaders =
		(struct Extra_Headers*) malloc(
			MAX_EXTRA_HEADERS * sizeof(struct Extra_Headers));
	if (!extra_headers) {
		return HTTP_INTERNAL_SERVER_ERROR;
	}
	for (const auto& header : mhdt->headers) {
		/* find header type. */
		index = httpheader_str2int(header.first);
		if (index < 0) {
			extra_headers->name = (char *)malloc(header.first.size() + 1);
			extra_headers->value = (char *)malloc(header.second.size() + 1);
			if (!extra_headers->name || !extra_headers->value) {
				/* cleanup will be made by caller */
				return HTTP_INTERNAL_SERVER_ERROR;
			}
			strcpy(extra_headers->name, header.first.c_str());
			strcpy(extra_headers->value, header.second.c_str());
			extra_headers->resp = NULL;

			extra_headers++;
			nb_extra++;

			if (nb_extra == MAX_EXTRA_HEADERS - 1) {
				break;
			}
		}
	}
	extra_headers->name = extra_headers->value = extra_headers->resp = NULL;
	return HTTP_OK;
}
static void FreeExtraHTTPHeaders(
	/*! [in] extra HTTP headers to free. */
	struct Extra_Headers *ExtraHeaders)
{
	struct Extra_Headers *extra_headers = ExtraHeaders;

	if (!ExtraHeaders) {
		return;
	}

	while (extra_headers->name) {
		free(extra_headers->name);
		if (extra_headers->value) free(extra_headers->value);
		if (extra_headers->resp) free(extra_headers->resp);
		extra_headers++;
	}

	free(ExtraHeaders);
}
#else
static int ExtraHTTPHeaders(MHDTransaction *, char **ExtraHeaders)
{
	*ExtraHeaders = nullptr;
	return HTTP_OK;
}
static void FreeExtraHTTPHeaders(char *extraheaders)
{
	if (extraheaders)
		free(extraheaders);
}
#endif


/*!
 * \brief Processes the request and returns the result in the output parameters.
 *
 * \return
 * \li \c HTTP_BAD_REQUEST
 * \li \c HTTP_INTERNAL_SERVER_ERROR
 * \li \c HTTP_REQUEST_RANGE_NOT_SATISFIABLE
 * \li \c HTTP_FORBIDDEN
 * \li \c HTTP_NOT_FOUND
 * \li \c HTTP_NOT_ACCEPTABLE
 * \li \c HTTP_OK
 */
static int process_request(
	MHDTransaction *mhdt,
	/*! [out] Tpye of response. */
	enum resp_type *rtype,
	/*! [out] Headers. */
	std::map<std::string, std::string>& headers,
	/*! [out] Get filename from request document. */
	std::string& filename,
	/*! [out] Send Instruction object where the response is set up. */
	struct SendInstruction *RespInstr)
{
	int code;
	int err_code;
	struct File_Info finfo;
	LocalDoc localdoc;
	
	assert(mhdt->method == HTTPMETHOD_GET ||
		   mhdt->method == HTTPMETHOD_HEAD ||
		   mhdt->method == HTTPMETHOD_POST ||
		   mhdt->method == HTTPMETHOD_SIMPLEGET);

	if (mhdt->method == HTTPMETHOD_POST) {
		return HTTP_FORBIDDEN;
	}

	/* init */
	err_code = HTTP_INTERNAL_SERVER_ERROR;	/* default error */
	const VirtualDirListEntry *entryp{nullptr};
	
	/* Unescape and canonize the path. Note that MHD has already
       stripped a possible query part ("?param=value...)  for us */
    std::string request_doc = remove_escaped_chars(mhdt->url);
	request_doc = remove_dots(request_doc);
	if (request_doc.empty()) {
		err_code = HTTP_FORBIDDEN;
		goto error_handler;
	}
	if (request_doc[0] != '/') {
		/* no slash */
		err_code = HTTP_BAD_REQUEST;
		goto error_handler;
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
		if ((code = ExtraHTTPHeaders(mhdt, &finfo.extra_headers)) != HTTP_OK) {
			err_code = code;
			goto error_handler;
		}
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
		if (virtualDirCallback.get_info(filename.c_str(), &finfo,
										entryp->cookie) != 0) {
			err_code = HTTP_NOT_FOUND;
			goto error_handler;
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
			if ((virtualDirCallback.get_info(filename.c_str(), &finfo,
											 entryp->cookie)
				 != UPNP_E_SUCCESS) || finfo.is_directory) {
				err_code = HTTP_NOT_FOUND;
				goto error_handler;
			}
		}
		/* not readable */
		if (!finfo.is_readable) {
			err_code = HTTP_FORBIDDEN;
			goto error_handler;
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
		if (gDocumentRootDir.size() == 0) {
			goto error_handler;
		}
		/* get file name */
		filename = gDocumentRootDir;
		filename += request_doc;
		/* remove trailing slashes */
		while (filename.size() > 0 && filename.back() == '/') {
			filename.pop_back();
		}
		/* get info on file */
		if (get_file_info(filename.c_str(), &finfo) != 0) {
			err_code = HTTP_NOT_FOUND;
			goto error_handler;
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
				err_code = HTTP_NOT_FOUND;
				goto error_handler;
			}
		}
		/* not readable */
		if (!finfo.is_readable) {
			err_code = HTTP_FORBIDDEN;
			goto error_handler;
		}
	}
	RespInstr->ReadSendSize = finfo.file_length;
	//std::cerr << "process_request: readsz: " << RespInstr->ReadSendSize <<"\n";
		
	/* Check other header field. */
	if ((code = CheckOtherHTTPHeaders(mhdt, RespInstr, finfo.file_length))
		!= HTTP_OK) {
		err_code = code;
		goto error_handler;
	}

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
	/* simple get http 0.9 as specified in http 1.0 */
	/* don't send headers */
	if (mhdt->method == HTTPMETHOD_SIMPLEGET) {
		headers.clear();
	}
	err_code = HTTP_OK;

error_handler:
	FreeExtraHTTPHeaders(finfo.extra_headers);
	return err_code;
}

class VFileReaderCtxt {
public:
	~VFileReaderCtxt() {
	}
	UpnpWebFileHandle fp{nullptr};
	const void *cookie;
};

static ssize_t vFileReaderCallback(void *cls, uint64_t pos, char *buf,
								   size_t max)
{
	VFileReaderCtxt *ctx = (VFileReaderCtxt*)cls;
	if (nullptr == ctx->fp) {
		UpnpPrintf(UPNP_ERROR, MSERV, __FILE__, __LINE__,
				   "vFileReaderCallback: fp is null !\n");
		return -1;
	}
	//std::cerr << "vFileReaderCallback: pos " << pos << " cnt " << max << "\n";
#if NOT_FOR_GERBERA_IT_LOCKS_UP
	if (virtualDirCallback.seek(ctx->fp, pos, SEEK_SET, ctx->cookie) !=
		(int64_t)pos) {
		return -1;
	}
	//std::cerr << "vFileReaderCallback: seek returned\n";
#endif
	int ret = virtualDirCallback.read(ctx->fp, buf, max, ctx->cookie);
	//std::cerr << "vFileReaderCallback: read got " << ret << "\n";
	return ret;
}

static void vFileFreeCallback (void *cls)
{
	if (cls) {
		VFileReaderCtxt *ctx = (VFileReaderCtxt*)cls;
		virtualDirCallback.close(ctx->fp, ctx->cookie);
		delete ctx;
	}
}

void web_server_callback(MHDTransaction *mhdt)
{
	int ret;
	enum resp_type rtype = (enum resp_type)0;
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
				struct stat st;
				fstat(fd, &st);
				mhdt->response = MHD_create_response_from_fd(st.st_size, fd);
				mhdt->httpstatus = 200;
			}
		}
		break;
		case RESP_WEBDOC:
		{
			VFileReaderCtxt *ctxt = new VFileReaderCtxt;
			ctxt->fp = virtualDirCallback.open(filename.c_str(), UPNP_READ,
											   RespInstr.cookie);
			ctxt->cookie = RespInstr.cookie;
			mhdt->response = MHD_create_response_from_callback(
				RespInstr.ReadSendSize, 4096, vFileReaderCallback,
				ctxt, vFileFreeCallback);
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
		//std::cerr << "process_request: adding header [" << header.first <<
		// "]->[" << header.second << "]\n";
		MHD_add_response_header(mhdt->response, header.first.c_str(),
								header.second.c_str());
	}
	
	UpnpPrintf(UPNP_INFO, HTTP, __FILE__, __LINE__,
			   "webserver: request processed...\n");
}
#endif /* EXCLUDE_WEB_SERVER */

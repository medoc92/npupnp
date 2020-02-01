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
 * \brief Defines the Web Server and has functions to carry out
 * operations of the Web Server.
 */

#include "config.h"


#if EXCLUDE_WEB_SERVER == 0

#include "webserver.h"

#include <map>
#include <iostream>
#include <inttypes.h>

#include <upnp/ixml.h>

#include "httputils.h"
#include "ithread.h"
#include "ssdplib.h"
#include "statcodes.h"
#include "upnp.h"
#include "upnpapi.h"
#include "UpnpStdInt.h"
#include "upnputil.h"
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

static ithread_mutex_t gWebMutex;

/*!
 * \brief Based on the extension, returns MIME type as malloc'd char*
 * If content type and sub type are not found, unknown types are used. 
*/
static UPNP_INLINE int get_content_type(
    const char *filename, DOMString *content_type)
{
	const char *ctname = "application/octet-stream";

	*content_type = NULL;

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
	*content_type = strdup(ctname);
	return *content_type ? 0 : UPNP_E_OUTOF_MEMORY;
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
	ithread_mutex_lock(&gWebMutex);
	localDocs[path] = doc;
	ithread_mutex_unlock(&gWebMutex);
	return UPNP_E_SUCCESS;
}

int web_server_unset_localdoc(const std::string& path)
{
	ithread_mutex_lock(&gWebMutex);
	auto it = localDocs.find(path);
	if (it != localDocs.end())
		localDocs.erase(it);
	ithread_mutex_unlock(&gWebMutex);
	return UPNP_E_SUCCESS;
}

int web_server_init()
{
	int ret = 0;

	if (bWebServerState == WEB_SERVER_DISABLED) {

		/* Initialize callbacks */
		virtualDirCallback.get_info = NULL;
		virtualDirCallback.open = NULL;
		virtualDirCallback.read = NULL;
		virtualDirCallback.write = NULL;
		virtualDirCallback.seek = NULL;
		virtualDirCallback.close = NULL;

		if (ithread_mutex_init(&gWebMutex, NULL) == -1)
			ret = UPNP_E_OUTOF_MEMORY;
		else
			bWebServerState = WEB_SERVER_ENABLED;
	}

	return ret;
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
		ithread_mutex_destroy(&gWebMutex);
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
	ixmlFreeDOMString(info->content_type);	
	info->content_type = NULL;
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
	int rc = get_content_type(filename, &info->content_type);
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

/*!
 * \brief Compares filePath with paths from the list of virtual directory
 * lists.
 *
 * \return nullptr or entry pointer.
 */
static const VirtualDirListEntry *isFileInVirtualDir(
	/*! [in] Directory path to be tested for virtual directory. */
	char *filePath)
{
	for (const auto& vd : virtualDirList) {
		if (vd.path.size()) {
			if (vd.path.back() == '/') {
				if (strncmp(vd.path.c_str(), filePath, vd.path.size()) == 0)
					return &vd;
			} else {
				if (strncmp(vd.path.c_str(), filePath, vd.path.size()) == 0 &&
				    (filePath[vd.path.size()] == '/' ||
				     filePath[vd.path.size()] == 0 ||
				     filePath[vd.path.size()] == '?'))
					return &vd;
			}
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
				/*  TODO? */
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
		if (extra_headers->resp) ixmlFreeDOMString(extra_headers->resp);
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
	char *request_doc = NULL;
	struct File_Info finfo;
	size_t dummy;
	LocalDoc localdoc;
	
	assert(mhdt->method == HTTPMETHOD_GET ||
	       mhdt->method == HTTPMETHOD_HEAD ||
	       mhdt->method == HTTPMETHOD_POST ||
	       mhdt->method == HTTPMETHOD_SIMPLEGET);

	if (mhdt->method == HTTPMETHOD_POST) {
		return HTTP_FORBIDDEN;
	}

	/* init */
	memset(&finfo, 0, sizeof(finfo));
	request_doc = NULL;
	err_code = HTTP_INTERNAL_SERVER_ERROR;	/* default error */
	const VirtualDirListEntry *entryp{nullptr};
	
	/* remove dots and unescape url */
	request_doc = strdup(mhdt->url.c_str());
	if (request_doc == NULL) {
		goto error_handler;	/* out of mem */
	}
	dummy = mhdt->url.size();
	remove_escaped_chars(request_doc, &dummy);
	code = remove_dots(request_doc, mhdt->url.size());
	if (code != 0) {
		err_code = HTTP_FORBIDDEN;
		goto error_handler;
	}
	if (*request_doc != '/') {
		/* no slash */
		err_code = HTTP_BAD_REQUEST;
		goto error_handler;
	}
	entryp = isFileInVirtualDir(request_doc);
	if (!entryp) {
		ithread_mutex_lock(&gWebMutex);
		auto localdocit = localDocs.find(request_doc);
		// Just make a copy. Could do better using a
		// map<string,share_ptr> like the original, but I don't think
		// that the perf impact is significant
		if (localdocit != localDocs.end()) {
			localdoc = localdocit->second;
		}
		ithread_mutex_unlock(&gWebMutex);
	}
	if (entryp) {
		*rtype = RESP_WEBDOC;
		RespInstr->cookie = entryp->cookie;
		filename = request_doc;
		if ((code = ExtraHTTPHeaders(mhdt, &finfo.extra_headers)) != HTTP_OK) {
			err_code = code;
			goto error_handler;
		}
		/* get file info */
		if (virtualDirCallback.get_info(filename.c_str(), &finfo,
										entryp->cookie) != 0) {
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
		finfo.content_type = strdup("text/xml");
		finfo.file_length = localdoc.data.size();
		finfo.is_readable = TRUE;
		finfo.is_directory = FALSE;
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
	/* Check other header field. */
	if ((code = CheckOtherHTTPHeaders(mhdt, RespInstr, finfo.file_length))
		!= HTTP_OK) {
		err_code = code;
		goto error_handler;
	}

	if (finfo.content_type && *finfo.content_type) {
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
	free(request_doc);
	FreeExtraHTTPHeaders(finfo.extra_headers);
	ixmlFreeDOMString(finfo.content_type);

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
		std::cerr << "vFileReaderCallback: fp is null !\n";
		return -1;
	}
	if (virtualDirCallback.seek(ctx->fp, pos, SEEK_SET, ctx->cookie) !=
		(int64_t)pos) {
		return -1;
	}
	int ret = virtualDirCallback.read(ctx->fp, buf, max, ctx->cookie);
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

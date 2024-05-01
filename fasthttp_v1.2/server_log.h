#ifndef __server_log_incl__
#define __server_log_incl__

#include <iostream>

#include "http_worker/http_worker.h"

extern bool SERVER_LOG_DISABLED;
extern bool SERVER_LOG_LOCALTIME_REPORTING;

extern std::mutex SERVER_LOG_WRITE_NORMAL;
extern std::mutex SERVER_LOG_WRITE_ERROR;

void SERVER_LOG_INIT(const char* info_file,const char* error_file);
std::string SERVER_LOG_strtime(bool local);

template<typename T> static inline void SERVER_LOG_WRITE(T msg, bool is_error = false)
{
	if(SERVER_LOG_DISABLED)
	{
		return;
	}

	if(is_error)
	{
		std::cerr << msg;
		std::cerr.flush();
	}
	else
	{
		std::cout << msg;
		std::cout.flush();
	}
}


void SERVER_ERROR_LOG_stdlib_err(const char* s);
void SERVER_ERROR_LOG_stdlib_err(int n, ...);

#ifndef DISABLE_HTTPS
void SERVER_ERROR_LOG_openssl_err(const char* s);
#endif

void SERVER_ERROR_LOG_conn_exceeded();
void SERVER_LOG_REQUEST(struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id);

#endif

#include <iostream>
#include <fstream>
#include <mutex>
#include <cstring>
#include <cstdarg>
#include <errno.h>


#ifndef DISABLE_HTTPS
#include <openssl/err.h>
#endif

#include "server_log.h"
#include "helper_functions.h"

bool SERVER_LOG_DISABLED = false;
bool SERVER_LOG_LOCALTIME_REPORTING = false;

std::mutex SERVER_LOG_WRITE_NORMAL;
std::mutex SERVER_LOG_WRITE_ERROR;

std::ofstream error_file_stream;
std::ofstream info_file_stream;

static inline size_t get_formated_time(time_t* t,char* buff,size_t buff_size,bool local)
{
	return strftime(buff,buff_size,"[%Y-%m-%d %H:%M:%S]",(local ? localtime(t) : gmtime(t)));
}

void SERVER_LOG_INIT(const char* info_file, const char* error_file)
{
	if(error_file != NULL)
	{
		error_file_stream.open(error_file, std::ios::app);
		
		if(!error_file_stream.is_open())
		{
			std::cerr << SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING) <<" Unable to open error log file ( " << error_file << " )" << std::endl;
			std::cerr << "Error code: 0x" << std::hex << errno << std::endl << strerror(errno) << std::endl << std::endl << std::dec;
		}
		else
		{
			std::cerr.rdbuf(error_file_stream.rdbuf());
		}
	}

	if(info_file != NULL)
	{
		info_file_stream.open(info_file, std::ios::app);
		if(!info_file_stream.is_open())
		{
			SERVER_ERROR_LOG_stdlib_err(3,"Unable to open log file ( ",info_file,")");
		}
		else
		{
			std::cout.rdbuf(info_file_stream.rdbuf());
		}
	}

}

std::string SERVER_LOG_strtime(bool local)
{
	char buffer[32];
	time_t t = time(NULL);
	get_formated_time(&t,buffer,32,local);
	std::string result = buffer;
	return result;
}

void SERVER_ERROR_LOG_stdlib_err(const char *s)
{
	if(SERVER_LOG_DISABLED)
	{
		return;
	}

	SERVER_LOG_WRITE_ERROR.lock();
	SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING), true);
	SERVER_LOG_WRITE(" ", true);
	SERVER_LOG_WRITE(s, true);
	SERVER_LOG_WRITE("\nError code: 0x", true);
	SERVER_LOG_WRITE(std::hex, true);
	SERVER_LOG_WRITE(errno, true);
	SERVER_LOG_WRITE("\n", true);
	SERVER_LOG_WRITE(strerror(errno), true);
	SERVER_LOG_WRITE("\n\n", true);
	SERVER_LOG_WRITE(std::dec, true);
	SERVER_LOG_WRITE_ERROR.unlock();
}

void SERVER_ERROR_LOG_stdlib_err(int n, ...)
{
	if(SERVER_LOG_DISABLED)
	{
		return;
	}

	SERVER_LOG_WRITE_ERROR.lock();
	SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING), true);
	SERVER_LOG_WRITE(" ", true);

	va_list arg_list;
	va_start(arg_list, n);
	for (int i = 0; i < n; i++)
	{
		SERVER_LOG_WRITE(va_arg(arg_list, const char *), true);
	}
	va_end(arg_list);

	SERVER_LOG_WRITE("\nError code: 0x", true);
	SERVER_LOG_WRITE(std::hex, true);
	SERVER_LOG_WRITE(errno, true);
	SERVER_LOG_WRITE("\n", true);
	SERVER_LOG_WRITE(strerror(errno), true);
	SERVER_LOG_WRITE("\n\n", true);
	SERVER_LOG_WRITE(std::dec, true);
	SERVER_LOG_WRITE_ERROR.unlock();
}

#ifndef DISABLE_HTTPS
static int get_openssl_error_callback(const char *str,size_t,void*)
{
	SERVER_LOG_WRITE(str,true);
	SERVER_LOG_WRITE("\n",true);
	
	return 0;
}

void SERVER_ERROR_LOG_openssl_err(const char* s)
{
	if(SERVER_LOG_DISABLED)
	{
	  	return;
	}

	SERVER_LOG_WRITE_ERROR.lock();
	SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING),true); 
	SERVER_LOG_WRITE(" ",true);
	SERVER_LOG_WRITE(s,true);
	SERVER_LOG_WRITE("\n--OpenSSL errors start--\n",true);
	ERR_print_errors_cb(get_openssl_error_callback,0);
	SERVER_LOG_WRITE("--OpenSSL errors end--\n\n",true);
	SERVER_LOG_WRITE_ERROR.unlock();
}
#endif

void SERVER_ERROR_LOG_conn_exceeded()
{
	if (SERVER_LOG_DISABLED)
	{
		return;
	}

	SERVER_LOG_WRITE_ERROR.lock();
	SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING), true);
	SERVER_LOG_WRITE(" Unable to accept the new connection!\n", true);
	SERVER_LOG_WRITE("Connection concurrency limit exceeded!\n\n", true);
	SERVER_LOG_WRITE_ERROR.unlock();
}

/*
FORMAT:
[time] PROCESSED_REQUEST: hostname:port client_ip:port protocol_version request_method "URI_path" http_status "user_agent"
*/
void SERVER_LOG_REQUEST(struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id)
{
	if (SERVER_LOG_DISABLED)
	{
		return;
	}

	struct HTTP_REQUEST* http_request;
	struct HTTP_RESPONSE* http_response;

	if(conn->http_version == HTTP_VERSION_2)
	{
		struct HTTP2_CONNECTION* http2_conn = (struct HTTP2_CONNECTION*) conn->raw_connection;

		http_request = &http2_conn->streams[stream_id].request;
		http_response = &http2_conn->streams[stream_id].response;
	}
	else
	{
		struct HTTP1_CONNECTION* http_conn = (struct HTTP1_CONNECTION*) conn->raw_connection;

		http_request = &http_conn->request;
		http_response = &http_conn->response;
	}

	SERVER_LOG_WRITE_NORMAL.lock();
	SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING));
	SERVER_LOG_WRITE(" HTTP REQUEST PROCESSED: ");

	auto hostname_it = http_request->headers.find("host");
	if (hostname_it != http_request->headers.end())
	{
		SERVER_LOG_WRITE(hostname_it->second);
	}
	else
	{
		SERVER_LOG_WRITE("-");
	}

	SERVER_LOG_WRITE(":");
	SERVER_LOG_WRITE(conn->server_port);
	SERVER_LOG_WRITE(" ");
	SERVER_LOG_WRITE(conn->remote_addr);
	SERVER_LOG_WRITE(":");
	SERVER_LOG_WRITE(conn->remote_port);
	SERVER_LOG_WRITE(" ");

	if (conn->http_version == HTTP_VERSION_1)
	{
		SERVER_LOG_WRITE("HTTP/1 ");
	}
	else if (conn->http_version == HTTP_VERSION_1_1)
	{
		SERVER_LOG_WRITE("HTTP/1.1 ");
	}
	else if (conn->http_version == HTTP_VERSION_2)
	{
		SERVER_LOG_WRITE("HTTP/2 ");
	}
	else
	{
		SERVER_LOG_WRITE("- ");
	}

	if (http_request->method == HTTP_METHOD_GET)
	{
		SERVER_LOG_WRITE("GET ");
	}
	else if (http_request->method == HTTP_METHOD_POST)
	{
		SERVER_LOG_WRITE("POST ");
	}
	else if (http_request->method == HTTP_METHOD_HEAD)
	{
		SERVER_LOG_WRITE("HEAD ");
	}
	else if (http_request->method == HTTP_METHOD_PUT)
	{
		SERVER_LOG_WRITE("PUT ");
	}
	else if (http_request->method == HTTP_METHOD_DELETE)
	{
		SERVER_LOG_WRITE("DELETE ");
	}
	else if (http_request->method == HTTP_METHOD_OPTIONS)
	{
		SERVER_LOG_WRITE("OPTIONS ");
	}
	else if (http_request->method == HTTP_METHOD_CONNECT)
	{
		SERVER_LOG_WRITE("CONNECT ");
	}
	else if (http_request->method == HTTP_METHOD_TRACE)
	{
		SERVER_LOG_WRITE("TRACE ");
	}
	else if (http_request->method == HTTP_METHOD_PATCH)
	{
		SERVER_LOG_WRITE("PATCH ");
	}
	else
	{
		SERVER_LOG_WRITE("- ");
	}

	SERVER_LOG_WRITE("\"");
	if (!http_request->URI_path.empty())
	{
		SERVER_LOG_WRITE(http_request->URI_path);
		if (!http_request->URI_query.empty())
		{
			SERVER_LOG_WRITE("?");

			auto i = http_request->URI_query.begin();
			auto next_elem = i;
			next_elem++;

			for (; i != http_request->URI_query.end(); ++i)
			{
				SERVER_LOG_WRITE(url_encode(&(i->first)));
				if (!i->second.empty())
				{
					SERVER_LOG_WRITE("=");
					SERVER_LOG_WRITE(url_encode(&(i->second)));
				}

				if (next_elem != http_request->URI_query.end())
				{
					SERVER_LOG_WRITE("&");
					next_elem++;
				}
			}
		}
	}
	SERVER_LOG_WRITE("\" ");

	SERVER_LOG_WRITE(http_response->code);
	SERVER_LOG_WRITE(" \"");

	auto UA_it = http_request->headers.find("user-agent");
	if (UA_it != http_request->headers.end())
	{
		SERVER_LOG_WRITE(UA_it->second);
	}
	else
	{
		SERVER_LOG_WRITE("-");
	}

	SERVER_LOG_WRITE("\"\n");
	SERVER_LOG_WRITE_NORMAL.unlock();
}

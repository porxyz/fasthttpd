#ifndef __server_journal_incl__
#define __server_journal_incl__

#include <iostream>
#include <string>  
#include <mutex> 

extern bool SERVER_JOURNAL_DISABLED;
extern bool SERVER_JOURNAL_LOCALTIME_REPORTING;

extern std::mutex SERVER_JOURNAL_WRITE_NORMAL;
extern std::mutex SERVER_JOURNAL_WRITE_ERROR;

void SERVER_JOURNAL_INIT(const char* info_file,const char* error_file);
std::string journal_strtime(bool local);

template<typename T> static inline void SERVER_JOURNAL_WRITE(T msg,bool is_error = false)
{
	if(SERVER_JOURNAL_DISABLED)
		return;

	if(is_error)
		std::cerr << msg;

	else
		std::cout << msg;
}


void SERVER_ERROR_JOURNAL_stdlib_err(const char* s);
void SERVER_ERROR_JOURNAL_stdlib_err(int n, ...);

#ifndef DISABLE_HTTPS
void SERVER_ERROR_JOURNAL_openssl_err(const char* s);
#endif

void SERVER_ERROR_JOURNAL_conn_exceeded();



#endif

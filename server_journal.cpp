#include <iostream>
#include <fstream>
#include <ctime>
#include <mutex>
#include <errno.h>
#include <string.h>


#include <openssl/err.h>


#include "server_journal.h"
#include "helper_functions.h"


bool SERVER_JOURNAL_DISABLED = false;
bool SERVER_JOURNAL_LOCALTIME_REPORTING = false;

std::mutex SERVER_JOURNAL_WRITE_NORMAL;
std::mutex SERVER_JOURNAL_WRITE_ERROR;

void SERVER_JOURNAL_INIT(const char* info_file,const char* error_file)
{
	if(error_file != NULL)
	{
		std::ofstream error_file_stream;
		error_file_stream.open(error_file);
		
		if(!error_file_stream.is_open())
		{
			std::cerr << journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING) <<" Unable to open error log file ( " << error_file << " )" << std::endl;
			std::cerr << "Error code: 0x" << std::hex << errno << std::endl << strerror(errno) << std::endl << std::endl << std::dec;
			error_file_stream.close();
		}
		
		else
			std::cerr.rdbuf(error_file_stream.rdbuf());
		
	}

	if(info_file != NULL)
	{
		std::ofstream info_file_stream;
		info_file_stream.open(info_file);
		if(!info_file_stream.is_open())
		{
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
			SERVER_JOURNAL_WRITE(" Unable to open log file (",true);
			SERVER_JOURNAL_WRITE(info_file,true);
			SERVER_JOURNAL_WRITE(" )\nError code: 0x",true);
			SERVER_JOURNAL_WRITE(std::hex,true);
			SERVER_JOURNAL_WRITE(errno,true);
			SERVER_JOURNAL_WRITE("\n",true);
			SERVER_JOURNAL_WRITE(strerror(errno),true);
			SERVER_JOURNAL_WRITE(std::dec,true);
			SERVER_JOURNAL_WRITE("\n\n",true);
			info_file_stream.close();
		}
		else
			std::cout.rdbuf(info_file_stream.rdbuf());
	}

}


inline size_t get_formated_time(time_t* t,char* buff,size_t buff_size,bool local)
{
	return strftime(buff,buff_size,"[%Y-%m-%d %H:%M:%S]",(local ? localtime(t) : gmtime(t)));
}



static int get_openssl_error_callback(const char *str,size_t,void*)
{
	SERVER_JOURNAL_WRITE(str,true);
	SERVER_JOURNAL_WRITE("\n",true);
	return 0;
}


std::string journal_strtime(bool local)
{
	char buffer[32];
	time_t t = time(NULL);
	get_formated_time(&t,buffer,32,local);
	std::string result = buffer;
	return result;
}	


void SERVER_ERROR_JOURNAL_stdlib_err(const char* s)
{
	  if(SERVER_JOURNAL_DISABLED)
	  	return;
	  	
	  SERVER_JOURNAL_WRITE_ERROR.lock();
          SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
          SERVER_JOURNAL_WRITE(" ",true);
	  SERVER_JOURNAL_WRITE(s,true);
	  SERVER_JOURNAL_WRITE("\nError code: 0x",true);
          SERVER_JOURNAL_WRITE(std::hex,true);
          SERVER_JOURNAL_WRITE(errno,true);
          SERVER_JOURNAL_WRITE("\n",true);
          SERVER_JOURNAL_WRITE(strerror(errno),true);
          SERVER_JOURNAL_WRITE("\n\n",true);
	  SERVER_JOURNAL_WRITE(std::dec,true);
          SERVER_JOURNAL_WRITE_ERROR.unlock();
}

void SERVER_ERROR_JOURNAL_openssl_err(const char* s)
{
	 if(SERVER_JOURNAL_DISABLED)
	  	return;
	  	
	SERVER_JOURNAL_WRITE_ERROR.lock();
	SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
	SERVER_JOURNAL_WRITE(" ",true);
	SERVER_JOURNAL_WRITE(s,true);
	SERVER_JOURNAL_WRITE("\n--OpenSSL errors start--\n",true);
	ERR_print_errors_cb(get_openssl_error_callback,0);
	SERVER_JOURNAL_WRITE("--OpenSSL errors end--\n\n",true);
	SERVER_JOURNAL_WRITE_ERROR.unlock();
}

void SERVER_ERROR_JOURNAL_conn_exceeded()
{
	if(SERVER_JOURNAL_DISABLED)
		return;
	  	
	SERVER_JOURNAL_WRITE_ERROR.lock();
	SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
        SERVER_JOURNAL_WRITE(" Unable to accept the new connection!\n",true);
        SERVER_JOURNAL_WRITE("Connection concurrency limit exceeded!\n\n",true);
	SERVER_JOURNAL_WRITE_ERROR.unlock();
}





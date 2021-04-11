#include <csignal>
#include <sys/resource.h>
#include <sys/eventfd.h>
#include <unistd.h>

#ifndef NO_MOD_MYSQL
#include <mysql/mysql.h>
#endif

#ifndef DISABLE_HTTPS
#include "https_listener.h"
#endif 

#include "helper_functions.h"
#include "server_config.h"
#include "server_journal.h"
#include "server_listener.h"
#include "http_worker.h"
#include "custom_bound.h"


int SERVER_CLOSE_TRIGGER;
void terminate_signal_handler(int)
{
        eventfd_t event_data = 0xdead; // dummy value
        if(eventfd_write(SERVER_CLOSE_TRIGGER,event_data) == -1)
        {
		SERVER_ERROR_JOURNAL_stdlib_err("Failed to trigger the server close eventfd!");
                exit(-1);
        }

}


int main(int argc,char** argv)
{

	if(argc == 2)
		load_server_config(argv[1]);
	else
		load_server_config(NULL);



	#ifndef NO_MOD_MYSQL
	if(mysql_library_init(0,NULL,NULL) != 0)
	{
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
		SERVER_JOURNAL_WRITE(" MOD MYSQL: Unable to init mysql client library!\n\n",true);
		return -1;
	}
	#endif


	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
	{
		SERVER_ERROR_JOURNAL_stdlib_err("Failed to disable SIGPIPE!");
		return -1;
	}


        if(signal(SIGINT, terminate_signal_handler) == SIG_ERR or signal(SIGTERM, terminate_signal_handler) == SIG_ERR)
        {
            SERVER_ERROR_JOURNAL_stdlib_err("Unable to register the terminate signal handler!");
            return -1;
        }

	if(server_config_variable_exists("priority"))
	{
		std::string process_priority = SERVER_CONFIGURATION["priority"];

		if(process_priority == "high")
		{
			if (setpriority(PRIO_PROCESS,getpid(),-40) != 0)
				SERVER_ERROR_JOURNAL_stdlib_err("Unable to set the process priority!");
		}

		if(process_priority == "low")
		{
			if (setpriority(PRIO_PROCESS,getpid(),40) != 0)
				SERVER_ERROR_JOURNAL_stdlib_err("Unable to set the process priority!");
		}

	}


	load_custom_bound_paths();

	
	int HTTP_LISTENER = init_server_listener_socket();
	if(HTTP_LISTENER == -1)
		return -1;

        SERVER_CLOSE_TRIGGER = eventfd(1,EFD_CLOEXEC | EFD_NONBLOCK);

	if(SERVER_CLOSE_TRIGGER == -1)
	{
                SERVER_ERROR_JOURNAL_stdlib_err("Unable to create the server close trigger!");
		return -1;
	}

        //perform a dummy read in order to make the eventfd buffer empty
        //if a dummy read is not performed, the epoll will be triggered instantly
        eventfd_t dummy_read;
        if(eventfd_read(SERVER_CLOSE_TRIGGER,&dummy_read) == -1)
        {
            SERVER_ERROR_JOURNAL_stdlib_err("Unable to perform a dummy read on the server close trigger!");
            return -1;
        }

	int SERVER_HTTP_EPOLL = init_server_listener_epoll(HTTP_LISTENER,SERVER_CLOSE_TRIGGER);
	if(SERVER_HTTP_EPOLL == -1)
		return -1;


	init_workers(SERVER_CLOSE_TRIGGER);

	#ifndef DISABLE_HTTPS
	std::thread https_listener_thread;
	if(is_server_config_variable_true("enable_https"))
		https_listener_thread = std::thread(https_listener_main,SERVER_CLOSE_TRIGGER);
	#endif
	
	if(run_server_listener_loop(SERVER_HTTP_EPOLL,HTTP_LISTENER) == -1)
		return -1;
	
	
	SERVER_JOURNAL_WRITE_NORMAL.lock();
	SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING)); 
	SERVER_JOURNAL_WRITE(" Received TERMINATE signal!\n");
	SERVER_JOURNAL_WRITE("The server is shutting down!\n\n");
	SERVER_JOURNAL_WRITE_NORMAL.unlock();


	// shutdown procedure
	
	#ifndef DISABLE_HTTPS
	if(is_server_config_variable_true("enable_https"))
		https_listener_thread.join();
	#endif


	std::thread force_shutdown_thread([](uint8_t wait_sec)
	{
		std::this_thread::sleep_for(std::chrono::seconds(wait_sec)); 
		SERVER_JOURNAL_WRITE_ERROR.lock();
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
		SERVER_JOURNAL_WRITE(" The server didn't shutdown in a timely manner!\nTerminating forcefully!\n\n",true);
		SERVER_JOURNAL_WRITE_ERROR.unlock();
		exit(-1);
	},str2uint(&SERVER_CONFIGURATION["shutdown_wait_timeout"]));

	force_shutdown_thread.detach();

	wait_for_workers_to_exit();

	close(SERVER_HTTP_EPOLL);
	close(HTTP_LISTENER);
	close(SERVER_CLOSE_TRIGGER);


	#ifndef NO_MOD_MYSQL
	mysql_library_end();
	#endif

	return 0;
}






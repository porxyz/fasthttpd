#include <iostream>
#include <csignal>
#include <cstring>

#ifndef NO_MOD_MYSQL
#include <mysql/mysql.h>
#endif

#ifndef DISABLE_HTTPS
#include "https_listener.h"
#endif 

#include "helper_functions.h"
#include "server_config.h"
#include "server_log.h"
#include "server_listener.h"
#include "custom_bound.h"
#include "http_worker/http_worker.h"

#include <unistd.h>
#include <sys/resource.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>

int SERVER_CLOSE_TRIGGER;
void terminate_signal_handler(int)
{
        eventfd_t event_data = 0xdead; // dummy value
        if(eventfd_write(SERVER_CLOSE_TRIGGER, event_data) == -1)
        {
			SERVER_ERROR_LOG_stdlib_err("Failed to trigger the server close eventfd!");
            exit(-1);
        }
}

void http_listener_main(int SERVER_CLOSE_TRIGGER)
{
	int HTTP_LISTENER = init_server_listener_socket();
	if(HTTP_LISTENER == -1)
	{
		exit(-1);
	}

	int SERVER_HTTP_EPOLL = init_server_listener_epoll(HTTP_LISTENER, SERVER_CLOSE_TRIGGER);
	if(SERVER_HTTP_EPOLL == -1)
	{
		exit(-1);
	}

	server_listener_parameters listener_params;
	listener_params.https = false;
	listener_params.http_listener = HTTP_LISTENER;
	listener_params.server_epoll = SERVER_HTTP_EPOLL;

	if(run_server_listener_loop(listener_params) == -1)
	{
		exit(-1);
	}

	close(SERVER_HTTP_EPOLL);
	close(HTTP_LISTENER);
}

int main(int argc, char** argv)
{
	if(argc >= 2)
	{
		load_server_config(argv[1]);
	}
	else
	{
		load_server_config(NULL);
	}

	#ifndef NO_MOD_MYSQL
	if(mysql_library_init(0,NULL,NULL) != 0)
	{
		SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING), true); 
		SERVER_LOG_WRITE(" MOD MYSQL: Unable to init mysql client library!\n\n", true);
		return -1;
	}
	#endif

	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
	{
		SERVER_ERROR_LOG_stdlib_err("Failed to disable SIGPIPE!");
		return -1;
	}

    if(signal(SIGINT, terminate_signal_handler) == SIG_ERR or signal(SIGTERM, terminate_signal_handler) == SIG_ERR)
    {
        SERVER_ERROR_LOG_stdlib_err("Unable to register the terminate signal handler!");
        return -1;
    }

	if(server_config_variable_exists("priority"))
	{
		std::string process_priority = SERVER_CONFIGURATION["priority"];

		if(process_priority == "high")
		{
			if (setpriority(PRIO_PROCESS,getpid(),-40) != 0)
			{
				SERVER_ERROR_LOG_stdlib_err("Unable to set the process priority!");
			}
		}

		else if(process_priority == "low")
		{
			if (setpriority(PRIO_PROCESS,getpid(),40) != 0)
			{
				SERVER_ERROR_LOG_stdlib_err("Unable to set the process priority!");
			}
		}
	}

	load_custom_bound_paths();

	SERVER_CLOSE_TRIGGER = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if(SERVER_CLOSE_TRIGGER == -1)
	{
        SERVER_ERROR_LOG_stdlib_err("Unable to create the server close trigger!");
		return -1;
	}

	HTTP_Workers_Init(SERVER_CLOSE_TRIGGER);

	std::vector<std::thread*> http_listener_threads;
	for(size_t i = 0; i < str2uint(SERVER_CONFIGURATION["server_listeners"]); i++)
	{
		http_listener_threads.push_back(new std::thread(http_listener_main, SERVER_CLOSE_TRIGGER));
	}

	#ifndef DISABLE_HTTPS
	std::vector<std::thread*> https_listener_threads;
	if(is_server_config_variable_true("enable_https"))
	{
		for(size_t i = 0; i < str2uint(SERVER_CONFIGURATION["server_listeners"]); i++)
		{
			https_listener_threads.push_back(new std::thread(https_listener_main, SERVER_CLOSE_TRIGGER));
		}
	}
	#endif
	
	//init a epoll to wait for the close event
	int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if(epoll_fd == -1)
	{
		SERVER_ERROR_LOG_stdlib_err("Unable to create epoll!");
		return -1;
	}

	struct epoll_event epoll_config;
	memset(&epoll_config, 0, sizeof(epoll_config));
	
	epoll_config.events = EPOLLIN | EPOLLET;
	epoll_config.data.fd = SERVER_CLOSE_TRIGGER;

	if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, SERVER_CLOSE_TRIGGER,&epoll_config) == -1)
	{	
        SERVER_ERROR_LOG_stdlib_err("Unable to add the server close trigger to epoll!");
		return -1;
	}

	bool should_stop = false;
	while(!should_stop)
	{
		int epoll_result = epoll_wait(epoll_fd, &epoll_config, 1, -1);

		if(epoll_result == -1)
		{
			if(errno != EINTR)
			{
				SERVER_ERROR_LOG_stdlib_err("Unable to listen to epoll!");
				return -1;
			}
		}

		//close trigger is fired
		else if(epoll_result > 0)
		{
			should_stop = true;
		}
	}
	
	SERVER_LOG_WRITE_NORMAL.lock();
	SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING)); 
	SERVER_LOG_WRITE(" Received TERMINATE signal!\n");
	SERVER_LOG_WRITE("The server is shutting down!\n\n");
	SERVER_LOG_WRITE_NORMAL.unlock();

	std::thread force_shutdown_thread([](uint8_t wait_sec)
	{
		std::this_thread::sleep_for(std::chrono::seconds(wait_sec)); 
		SERVER_LOG_WRITE_ERROR.lock();
		SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING),true); 
		SERVER_LOG_WRITE(" The server didn't shutdown in a timely manner!\nTerminating forcefully!\n\n",true);
		SERVER_LOG_WRITE_ERROR.unlock();

		exit(-1);
	},str2uint(&SERVER_CONFIGURATION["shutdown_wait_timeout"]));
	force_shutdown_thread.detach();

	// shutdown procedure
	for(size_t i = 0; i<http_listener_threads.size(); i++) 
	{
		http_listener_threads[i]->join();
		delete(http_listener_threads[i]);
	}

	#ifndef DISABLE_HTTPS
	if(is_server_config_variable_true("enable_https"))
	{
		for(size_t i = 0; i<https_listener_threads.size(); i++) 
		{
			https_listener_threads[i]->join();
			delete(https_listener_threads[i]);
		}
	}
	#endif

	HTTP_Workers_Join();
	close(epoll_fd);
	close(SERVER_CLOSE_TRIGGER);

	#ifndef NO_MOD_MYSQL
	mysql_library_end();
	#endif

	return 0;
}

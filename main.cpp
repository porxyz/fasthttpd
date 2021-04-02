#include <string>
#include <cstring>
#include <thread>
#include <csignal>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#ifndef NO_MOD_MYSQL
#include <mysql/mysql.h>
#endif

#include "helper_functions.h"
#include "server_config.h"
#include "server_journal.h"
#include "https_listener.h"
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

	int ip_addr_version = (SERVER_CONFIGURATION["ip_version"] == "4") ? AF_INET : AF_INET6;
	int HTTP_LISTENER = socket(ip_addr_version,SOCK_STREAM,IPPROTO_TCP);

	if(HTTP_LISTENER == -1)
	{
		SERVER_ERROR_JOURNAL_stdlib_err("Unable to create the http listener socket!");
		return -1;
	}

	if(set_socket_nonblock(HTTP_LISTENER) == -1)
	{
		SERVER_ERROR_JOURNAL_stdlib_err("Unable to put the http listener socket in nonblocking mode!");
		return -1;
	}

	if(ip_addr_version == AF_INET6)
	{
		if(is_server_config_variable_true("ipv6_dual_stack"))
		{
			int sock_opt = 0;     
			if(setsockopt(HTTP_LISTENER, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&sock_opt, sizeof(sock_opt)) == -1)
				SERVER_ERROR_JOURNAL_stdlib_err("Failed enabling ipv6 dual stack!");
				
		}

		if(is_server_config_variable_false("ipv6_dual_stack"))
		{
				int sock_opt = 1;     
				if(setsockopt(HTTP_LISTENER, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&sock_opt, sizeof(sock_opt)) == -1)
					SERVER_ERROR_JOURNAL_stdlib_err("Failed disabling ipv6 dual stack!");
	
		}
	}


	if(is_server_config_variable_true("reuse_addr"))
	{
		int enable = 1;
		if (setsockopt(HTTP_LISTENER, SOL_SOCKET, SO_REUSEADDR,&enable,sizeof(int)) == -1)
			SERVER_ERROR_JOURNAL_stdlib_err("Failed enabling reuse_addr option on the http listener socket!");

	}

	if(is_server_config_variable_false("reuse_addr"))
	{
		int enable = 0;
		if (setsockopt(HTTP_LISTENER, SOL_SOCKET, SO_REUSEADDR, &enable,sizeof(int)) == -1)
			SERVER_ERROR_JOURNAL_stdlib_err("Failed disabling reuse_addr option on the http listener socket!");
				
	}

	


	struct sockaddr_in6 HTTP_LISTENER_ADDR;
	memset(&HTTP_LISTENER_ADDR,0,sizeof(struct sockaddr_in6));

	struct sockaddr_in* HTTP_LISTENER_ADDR4=(struct sockaddr_in*)&HTTP_LISTENER_ADDR;
	struct sockaddr_in6* HTTP_LISTENER_ADDR6=&HTTP_LISTENER_ADDR;

	HTTP_LISTENER_ADDR.sin6_family=ip_addr_version;

	inet_pton(ip_addr_version,SERVER_CONFIGURATION["ip_addr"].c_str(),((ip_addr_version == AF_INET6) ? (void*)&HTTP_LISTENER_ADDR6->sin6_addr : (void*)&HTTP_LISTENER_ADDR4->sin_addr));

	if(ip_addr_version == AF_INET6)
		HTTP_LISTENER_ADDR6->sin6_port = htons(str2uint(&SERVER_CONFIGURATION["listen_http_port"]));
		
	else
		HTTP_LISTENER_ADDR4->sin_port = htons(str2uint(&SERVER_CONFIGURATION["listen_http_port"]));

	if(bind(HTTP_LISTENER,(struct sockaddr*)&HTTP_LISTENER_ADDR,((ip_addr_version == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in))) == -1)
	{
		SERVER_ERROR_JOURNAL_stdlib_err("Unable to bind the http listener socket!");
		return -1;
	}

	if(listen(HTTP_LISTENER,SOMAXCONN) == -1)
	{
                SERVER_ERROR_JOURNAL_stdlib_err("Unable to put the http listener socket in listening state!");
		return -1;
	}


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


	int SERVER_HTTP_EPOLL = epoll_create1(EPOLL_CLOEXEC);
	if(SERVER_HTTP_EPOLL == -1)
	{
		SERVER_ERROR_JOURNAL_stdlib_err("Unable to create http server epoll!");
		return -1;
	}


	struct epoll_event epoll_config;
	epoll_config.events = EPOLLIN | EPOLLET;
	epoll_config.data.fd = HTTP_LISTENER;

	if(epoll_ctl(SERVER_HTTP_EPOLL,EPOLL_CTL_ADD,HTTP_LISTENER,&epoll_config) == -1)
	{
		SERVER_ERROR_JOURNAL_stdlib_err("Unable to add http listener socket to epoll!");
		return -1;
	}

	epoll_config.events = EPOLLIN | EPOLLET;
	epoll_config.data.fd = SERVER_CLOSE_TRIGGER;

	if(epoll_ctl(SERVER_HTTP_EPOLL,EPOLL_CTL_ADD,SERVER_CLOSE_TRIGGER,&epoll_config) == -1)
	{
                SERVER_ERROR_JOURNAL_stdlib_err("Unable to add the server close trigger to epoll!");
		return -1;
	}


	init_workers(SERVER_CLOSE_TRIGGER);

	std::thread https_listener_thread;
	if(is_server_config_variable_true("enable_https"))
		https_listener_thread = std::thread(https_listener_main,SERVER_CLOSE_TRIGGER);
		
	

	unsigned int max_connections = str2uint(&SERVER_CONFIGURATION["max_connections"]);

	struct sockaddr incoming_addr;
	socklen_t incoming_addr_size = (ip_addr_version == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
	uint16_t incoming_port;
	char incoming_addr_str[INET6_ADDRSTRLEN];

	struct epoll_event triggered_event;
	while(true)
	{
		int epoll_result = epoll_wait(SERVER_HTTP_EPOLL,&triggered_event,1,-1);

		if(epoll_result == -1)
		{

			if(errno != EINTR)
			{
				SERVER_ERROR_JOURNAL_stdlib_err("Unable to listen to epoll!");
				return -1;
			}

		}

		else if(epoll_result > 0)
		{
			if(triggered_event.data.fd == HTTP_LISTENER)
			{
				if(triggered_event.events & EPOLLIN)
				{
					while(true)
					{
						int new_http_client = accept(HTTP_LISTENER,&incoming_addr,&incoming_addr_size);

						if(new_http_client == -1)
						{
							if(errno == EINTR)
								continue;
								
							if(errno == EAGAIN or errno == EWOULDBLOCK)
								break;

						
                                                        SERVER_ERROR_JOURNAL_stdlib_err("Unable to accept the incoming connection!");
							return -1;
						}


						
						if(total_http_connections >= max_connections)
						{
							SERVER_ERROR_JOURNAL_conn_exceeded();
							close(new_http_client);
							continue;
						}
						
                                                total_http_connections++;
                                                

						if(set_socket_nonblock(new_http_client) == -1)
						{
                                                        SERVER_ERROR_JOURNAL_stdlib_err("Unable to put the http client in nonblocking mode!");
							return -1;
						}

						struct sockaddr_in* incoming_addr4 = (sockaddr_in*)&incoming_addr;
						struct sockaddr_in6* incoming_addr6 = (sockaddr_in6*)&incoming_addr;

						if(inet_ntop(incoming_addr.sa_family,((incoming_addr.sa_family == AF_INET6) ? (void*)&incoming_addr6->sin6_addr : (void*)&incoming_addr4->sin_addr), 
							     incoming_addr_str,incoming_addr_size) == NULL)
						{
                                                        SERVER_ERROR_JOURNAL_stdlib_err("Unable to read the remote socket address!");
							return -1;
						}


						incoming_port=ntohs(((incoming_addr.sa_family == AF_INET6) ? incoming_addr6->sin6_port : incoming_addr4->sin_port));

						worker_add_client(new_http_client,incoming_addr_str,&SERVER_CONFIGURATION["ip_addr"],
						                  incoming_port,str2uint(&SERVER_CONFIGURATION["listen_http_port"]),incoming_addr.sa_family);

					}
				}

				else
				{
					SERVER_JOURNAL_WRITE_ERROR.lock();
					SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
                                        SERVER_JOURNAL_WRITE(" Unknown event occured in the http listener epoll!\nEvent: 0x",true);
					SERVER_JOURNAL_WRITE(std::hex,true);
					SERVER_JOURNAL_WRITE(triggered_event.events,true);
					SERVER_JOURNAL_WRITE("\n\n",true);
					SERVER_JOURNAL_WRITE(std::dec,true);
					SERVER_JOURNAL_WRITE_ERROR.unlock();
				}
			}

			else
				break;


		}

	}

	SERVER_JOURNAL_WRITE_NORMAL.lock();
	SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING)); 
	SERVER_JOURNAL_WRITE(" Received TERMINATE signal!\n");
	SERVER_JOURNAL_WRITE("The server is shutting down!\n\n");
	SERVER_JOURNAL_WRITE_NORMAL.unlock();

	// shutdown procedure
	if(is_server_config_variable_true("enable_https"))
		https_listener_thread.join();



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






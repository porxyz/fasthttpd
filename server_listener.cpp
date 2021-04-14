#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>


#include "server_listener.h"
#include "helper_functions.h"
#include "server_config.h"
#include "server_journal.h"
#include "http_worker.h"



int init_server_listener_socket(bool https)
{
	int ip_addr_version = (SERVER_CONFIGURATION["ip_version"] == "4") ? AF_INET : AF_INET6;
	int http_listener = socket(ip_addr_version,SOCK_STREAM,IPPROTO_TCP);

	if(http_listener == -1)
	{
		SERVER_ERROR_JOURNAL_stdlib_err("Unable to create the listener socket!");
		return -1;
	}

	if(set_socket_nonblock(http_listener) == -1)
	{
		SERVER_ERROR_JOURNAL_stdlib_err("Unable to put the listener socket in nonblocking mode!");
		return -1;
	}

	if(ip_addr_version == AF_INET6)
	{
		if(is_server_config_variable_true("ipv6_dual_stack"))
		{
			int sock_opt = 0;     
			if(setsockopt(http_listener, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&sock_opt, sizeof(sock_opt)) == -1)
				SERVER_ERROR_JOURNAL_stdlib_err("Failed enabling ipv6 dual stack!");
				
		}

		if(is_server_config_variable_false("ipv6_dual_stack"))
		{
				int sock_opt = 1;     
				if(setsockopt(http_listener, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&sock_opt, sizeof(sock_opt)) == -1)
					SERVER_ERROR_JOURNAL_stdlib_err("Failed disabling ipv6 dual stack!");

		}
	}


	if(is_server_config_variable_true("reuse_addr"))
	{
		int enable = 1;
		if (setsockopt(http_listener, SOL_SOCKET, SO_REUSEADDR,&enable,sizeof(int)) == -1)
			SERVER_ERROR_JOURNAL_stdlib_err("Failed enabling reuse_addr option on the listener socket!");

	}

	else if(is_server_config_variable_false("reuse_addr"))
	{
		int enable = 0;
		if (setsockopt(http_listener, SOL_SOCKET, SO_REUSEADDR, &enable,sizeof(int)) == -1)
			SERVER_ERROR_JOURNAL_stdlib_err("Failed disabling reuse_addr option on the listener socket!");
				
	}
	

	struct sockaddr_in6 http_listener_addr;
	memset(&http_listener_addr,0,sizeof(struct sockaddr_in6));

	struct sockaddr_in* http_listener_addr4 = (struct sockaddr_in*)&http_listener_addr;
	struct sockaddr_in6* http_listener_addr6 = &http_listener_addr;

	http_listener_addr.sin6_family=ip_addr_version;

	inet_pton(ip_addr_version,SERVER_CONFIGURATION["ip_addr"].c_str(),((ip_addr_version == AF_INET6) ? (void*)&http_listener_addr6->sin6_addr : (void*)&http_listener_addr4->sin_addr));

	uint16_t http_listener_port;
	if(https)
		http_listener_port = str2uint(&SERVER_CONFIGURATION["listen_https_port"]);
	else
		http_listener_port = str2uint(&SERVER_CONFIGURATION["listen_http_port"]);


	if(ip_addr_version == AF_INET6)
		http_listener_addr6->sin6_port = htons(http_listener_port);
		
	else
		http_listener_addr4->sin_port = htons(http_listener_port);

	if(bind(http_listener,(struct sockaddr*)&http_listener_addr,((ip_addr_version == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in))) == -1)
	{
		SERVER_ERROR_JOURNAL_stdlib_err("Unable to bind the listener socket!");
		return -1;
	}

	if(listen(http_listener,SOMAXCONN) == -1)
	{
                SERVER_ERROR_JOURNAL_stdlib_err("Unable to put the listener socket in listening state!");
		return -1;
	}


	return http_listener;
}

	
int init_server_listener_epoll(int listener_socket,int close_trigger)
{
	int server_epoll = epoll_create1(EPOLL_CLOEXEC);
	if(server_epoll == -1)
	{
		SERVER_ERROR_JOURNAL_stdlib_err("Unable to create server epoll!");
		return -1;
	}


	struct epoll_event epoll_config;
	memset(&epoll_config,0,sizeof(epoll_config)); //to suppress valgrind warnings
	
	epoll_config.events = EPOLLIN | EPOLLET;
	epoll_config.data.fd = listener_socket;

	if(epoll_ctl(server_epoll,EPOLL_CTL_ADD,listener_socket,&epoll_config) == -1)
	{
		SERVER_ERROR_JOURNAL_stdlib_err("Unable to add listener socket to epoll!");
		return -1;
	}

	epoll_config.events = EPOLLIN | EPOLLET;
	epoll_config.data.fd = close_trigger;

	if(epoll_ctl(server_epoll,EPOLL_CTL_ADD,close_trigger,&epoll_config) == -1)
	{	
               	SERVER_ERROR_JOURNAL_stdlib_err("Unable to add the server close trigger to epoll!");
		return -1;
	}
		
	return server_epoll;
	
}


int run_server_listener_loop(int server_epoll, int http_listener,bool https
								#ifndef DISABLE_HTTPS
								,SSL_CTX* openssl_ctx
								#endif 
								)
{
	unsigned int max_connections = str2uint(&SERVER_CONFIGURATION["max_connections"]);

	struct sockaddr incoming_addr;
	socklen_t incoming_addr_size = (SERVER_CONFIGURATION["ip_version"] == "6") ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
	uint16_t incoming_port;
	char incoming_addr_str[INET6_ADDRSTRLEN];
	
	uint16_t http_listener_port;
	if(https)
		http_listener_port = str2uint(&SERVER_CONFIGURATION["listen_https_port"]);
	else
		http_listener_port = str2uint(&SERVER_CONFIGURATION["listen_http_port"]);



	/*
	support different kernel buffer dimensions
	useful for reducing kernel memory if you work with small requests
	useful if you want larger TCP window
	*/
	bool resize_recv_kernel_buffer = false;
	bool resize_send_kernel_buffer = false;
	socklen_t  recv_kernel_buffer_size;
	socklen_t  send_kernel_buffer_size;
	
	if(server_config_variable_exists("recv_kernel_buffer_size"))
	{
		recv_kernel_buffer_size = 1024 * str2uint(&SERVER_CONFIGURATION["recv_kernel_buffer_size"],NULL);
		resize_recv_kernel_buffer = true;
	}
	
	if(server_config_variable_exists("send_kernel_buffer_size"))
	{
		send_kernel_buffer_size = 1024 * str2uint(&SERVER_CONFIGURATION["send_kernel_buffer_size"],NULL);
		resize_send_kernel_buffer = true;
	}
	

	struct epoll_event triggered_event;
	memset(&triggered_event,0,sizeof(struct epoll_event)); //to suppress valgrind warnings
	
	while(true)
	{
		int epoll_result = epoll_wait(server_epoll,&triggered_event,1,-1);

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
			if(triggered_event.data.fd == http_listener)
			{
				if(triggered_event.events & EPOLLIN)
				{
					while(true)
					{
						int new_http_client = accept(http_listener,&incoming_addr,&incoming_addr_size);

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
						
						if(resize_recv_kernel_buffer)
						{
							if(setsockopt(new_http_client,SOL_SOCKET, SO_RCVBUF, &recv_kernel_buffer_size, sizeof(socklen_t)) == -1)
								SERVER_ERROR_JOURNAL_stdlib_err("Unable to adjust client socket SO_RCVBUF!");
							
						}
						
						if(resize_send_kernel_buffer)
						{
							if(setsockopt(new_http_client,SOL_SOCKET, SO_SNDBUF, &send_kernel_buffer_size, sizeof(socklen_t)) == -1)
								SERVER_ERROR_JOURNAL_stdlib_err("Unable to adjust client socket SO_SNDBUF!");
							
						}
						

						worker_add_client(new_http_client,incoming_addr_str,&SERVER_CONFIGURATION["ip_addr"],incoming_port,http_listener_port,incoming_addr.sa_family,https
								  #ifndef DISABLE_HTTPS
						                  ,openssl_ctx
						                  #endif
						                  );
						                                   
					}
				}

				else
				{
					SERVER_JOURNAL_WRITE_ERROR.lock();
					SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
                                        SERVER_JOURNAL_WRITE(" Unknown event occured in the listener epoll!\nEvent: 0x",true);
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
	
	return 0;
}


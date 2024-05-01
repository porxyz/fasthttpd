#include <cstring>
#include <atomic>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>

#include "server_listener.h"
#include "helper_functions.h"
#include "server_config.h"
#include "server_log.h"
#include "http_worker/http_worker.h"


typedef struct 
{
	int http_listener;
	uint16_t http_listener_port;

	socklen_t incoming_addr_size;
	unsigned int max_connections;

	bool resize_recv_kernel_buffer;
	socklen_t recv_kernel_buffer_size;

	bool resize_send_kernel_buffer;
	socklen_t send_kernel_buffer_size;

	bool https;

	#ifndef DISABLE_HTTPS
	SSL_CTX* openssl_ctx;
	#endif

} accept_new_client_func_parameters;

int init_server_listener_socket(bool https)
{
	int ip_addr_version = (SERVER_CONFIGURATION["ip_version"] == "4") ? AF_INET : AF_INET6;
	int http_listener = socket(ip_addr_version, SOCK_STREAM, IPPROTO_TCP);

	if(http_listener == -1)
	{
		SERVER_ERROR_LOG_stdlib_err("Unable to create the listener socket!");
		return -1;
	}

	if(set_socket_nonblock(http_listener) == -1)
	{
		SERVER_ERROR_LOG_stdlib_err("Unable to put the listener socket in nonblocking mode!");
		return -1;
	}

	if(ip_addr_version == AF_INET6)
	{
		if(is_server_config_variable_true("ipv6_dual_stack"))
		{
			int sock_opt = 0;     
			if(setsockopt(http_listener, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&sock_opt, sizeof(sock_opt)) == -1)
			{
				SERVER_ERROR_LOG_stdlib_err("Failed enabling ipv6 dual stack!");
			}
		}

		if(is_server_config_variable_false("ipv6_dual_stack"))
		{
				int sock_opt = 1;     
				if(setsockopt(http_listener, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&sock_opt, sizeof(sock_opt)) == -1)
				{
					SERVER_ERROR_LOG_stdlib_err("Failed disabling ipv6 dual stack!");
				}
		}
	}

	if(is_server_config_variable_true("reuse_addr"))
	{
		int enable = 1;
		if (setsockopt(http_listener, SOL_SOCKET, SO_REUSEADDR,&enable,sizeof(int)) == -1)
		{
			SERVER_ERROR_LOG_stdlib_err("Failed enabling reuse_addr option on the listener socket!");
		}
	}
	else if(is_server_config_variable_false("reuse_addr"))
	{
		int enable = 0;
		if (setsockopt(http_listener, SOL_SOCKET, SO_REUSEADDR, &enable,sizeof(int)) == -1)
		{
			SERVER_ERROR_LOG_stdlib_err("Failed disabling reuse_addr option on the listener socket!");
		}		
	}

	if(is_server_config_variable_true("reuse_port"))
	{
		int enable = 1;
		if (setsockopt(http_listener, SOL_SOCKET, SO_REUSEPORT, &enable,sizeof(int)) == -1)
		{
			SERVER_ERROR_LOG_stdlib_err("Failed enabling reuse_port option on the listener socket!");
		}
	}
	else if(is_server_config_variable_false("reuse_port"))
	{
		int enable = 0;
		if (setsockopt(http_listener, SOL_SOCKET, SO_REUSEPORT, &enable,sizeof(int)) == -1)
		{
			SERVER_ERROR_LOG_stdlib_err("Failed disabling reuse_port option on the listener socket!");
		}		
	}
	
	struct sockaddr_in6 http_listener_addr;
	memset(&http_listener_addr,0,sizeof(struct sockaddr_in6));

	struct sockaddr_in* http_listener_addr4 = (struct sockaddr_in*)&http_listener_addr;
	struct sockaddr_in6* http_listener_addr6 = &http_listener_addr;

	http_listener_addr.sin6_family = ip_addr_version;

	inet_pton(ip_addr_version,SERVER_CONFIGURATION["ip_addr"].c_str(),((ip_addr_version == AF_INET6) ? (void*)&http_listener_addr6->sin6_addr : (void*)&http_listener_addr4->sin_addr));

	uint16_t http_listener_port;
	if(https)
	{
		http_listener_port = str2uint(&SERVER_CONFIGURATION["listen_https_port"]);
	}
	else
	{
		http_listener_port = str2uint(&SERVER_CONFIGURATION["listen_http_port"]);
	}

	if(ip_addr_version == AF_INET6)
	{
		http_listener_addr6->sin6_port = endian_conv_hton16(http_listener_port);
	}
	else
	{
		http_listener_addr4->sin_port = endian_conv_hton16(http_listener_port);
	}

	if(bind(http_listener,(struct sockaddr*)&http_listener_addr,((ip_addr_version == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in))) == -1)
	{
		SERVER_ERROR_LOG_stdlib_err("Unable to bind the listener socket!");
		return -1;
	}

	if(listen(http_listener, str2uint(&SERVER_CONFIGURATION["max_connections"])) == -1)
	{
        SERVER_ERROR_LOG_stdlib_err("Unable to put the listener socket in listening state!");
		return -1;
	}

	return http_listener;
}
	
int init_server_listener_epoll(int listener_socket, int close_trigger)
{
	int server_epoll = epoll_create1(EPOLL_CLOEXEC);
	if(server_epoll == -1)
	{
		SERVER_ERROR_LOG_stdlib_err("Unable to create server epoll!");
		return -1;
	}

	struct epoll_event epoll_config;
	memset(&epoll_config,0,sizeof(epoll_config)); //to suppress valgrind warnings
	
	epoll_config.events = EPOLLIN | EPOLLET;
	epoll_config.data.fd = listener_socket;

	if(epoll_ctl(server_epoll,EPOLL_CTL_ADD,listener_socket,&epoll_config) == -1)
	{
		SERVER_ERROR_LOG_stdlib_err("Unable to add listener socket to epoll!");
		return -1;
	}

	epoll_config.events = EPOLLIN | EPOLLET;
	epoll_config.data.fd = close_trigger;

	if(epoll_ctl(server_epoll,EPOLL_CTL_ADD,close_trigger,&epoll_config) == -1)
	{	
        SERVER_ERROR_LOG_stdlib_err("Unable to add the server close trigger to epoll!");
		return -1;
	}
		
	return server_epoll;
}

int accept_new_client(accept_new_client_func_parameters& params)
{
	struct sockaddr_in6 incoming_addr;
	char incoming_addr_str[INET6_ADDRSTRLEN];

	HTTP_Worker_Add_Client_Parameters add_client_params;
	add_client_params.https = params.https;
	add_client_params.server_addr = SERVER_CONFIGURATION["ip_addr"].c_str();
	add_client_params.server_port = params.http_listener_port;

	#ifndef DISABLE_HTTPS
	add_client_params.openssl_ctx = params.openssl_ctx;
	#endif

	bool should_stop = false;
	while (!should_stop)
	{
		add_client_params.client_sock = accept(params.http_listener, (struct sockaddr*) &incoming_addr, &params.incoming_addr_size);

		if (add_client_params.client_sock == -1)
		{
			if (errno == EINTR)
			{
				continue;
			}
			else if (errno == EAGAIN or errno == EWOULDBLOCK)
			{
				should_stop = true;
				continue;
			}

			SERVER_ERROR_LOG_stdlib_err("Unable to accept the incoming connection!");

			if (errno == EMFILE or errno == ENFILE)
			{
				continue;
			}

			return -1;
		}
		
		if (total_http_connections >= params.max_connections)
		{	
			SERVER_ERROR_LOG_conn_exceeded();
			close(add_client_params.client_sock);
			continue;
		}

		total_http_connections++;

		if (set_socket_nonblock(add_client_params.client_sock) == -1)
		{
			SERVER_ERROR_LOG_stdlib_err("Unable to put the http client in nonblocking mode!");
			return -1;
		}

		struct sockaddr_in *incoming_addr4 = (sockaddr_in*) &incoming_addr;

		const char* ip2text_result = NULL;

		if(incoming_addr.sin6_family == AF_INET6)
		{
			ip2text_result = inet_ntop(AF_INET6, (const void*) &incoming_addr.sin6_addr, incoming_addr_str, sizeof(incoming_addr_str));
		}
		else
		{
			ip2text_result = inet_ntop(AF_INET, (const void*) &incoming_addr4->sin_addr, incoming_addr_str, sizeof(incoming_addr_str));
		}

		if (ip2text_result == NULL)
		{
			SERVER_ERROR_LOG_stdlib_err("Unable to read the remote socket address!");
			return -1;
		}

		add_client_params.remote_addr = incoming_addr_str;
		add_client_params.remote_port = endian_conv_ntoh16(incoming_addr.sin6_port);
		add_client_params.ip_addr_version = incoming_addr.sin6_family;

		if (params.resize_recv_kernel_buffer)
		{
			if (setsockopt(add_client_params.client_sock, SOL_SOCKET, SO_RCVBUF, &params.recv_kernel_buffer_size, sizeof(socklen_t)) == -1)
			{
				SERVER_ERROR_LOG_stdlib_err("Unable to adjust client socket SO_RCVBUF!");
			}
		}

		if (params.resize_send_kernel_buffer)
		{
			if (setsockopt(add_client_params.client_sock, SOL_SOCKET, SO_SNDBUF, &params.send_kernel_buffer_size, sizeof(socklen_t)) == -1)
			{
				SERVER_ERROR_LOG_stdlib_err("Unable to adjust client socket SO_SNDBUF!");
			}
		}

		HTTP_Worker_Add_Client(add_client_params);
	}

	return 0;
}

int run_server_listener_loop(server_listener_parameters& params)
{
	accept_new_client_func_parameters accept_client_params;
	accept_client_params.http_listener = params.http_listener;

	accept_client_params.max_connections = str2uint(&SERVER_CONFIGURATION["max_connections"]);
	accept_client_params.incoming_addr_size = (SERVER_CONFIGURATION["ip_version"] == "6") ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
	
	if(params.https)
	{
		accept_client_params.http_listener_port = str2uint(&SERVER_CONFIGURATION["listen_https_port"]);
	}
	else
	{
		accept_client_params.http_listener_port = str2uint(&SERVER_CONFIGURATION["listen_http_port"]);
	}

	accept_client_params.https = params.https;

	#ifndef DISABLE_HTTPS
	accept_client_params.openssl_ctx = params.openssl_ctx;
	#endif

	/*
	support different kernel buffer dimensions
	useful for reducing kernel memory if you work with small requests
	useful if you want larger TCP window
	*/
	accept_client_params.resize_recv_kernel_buffer = false;
	accept_client_params.resize_send_kernel_buffer = false;
	
	if(server_config_variable_exists("recv_kernel_buffer_size"))
	{
		accept_client_params.recv_kernel_buffer_size = 1024 * str2uint(&SERVER_CONFIGURATION["recv_kernel_buffer_size"]);
		accept_client_params.resize_recv_kernel_buffer = true;
	}
	
	if(server_config_variable_exists("send_kernel_buffer_size"))
	{
		accept_client_params.send_kernel_buffer_size = 1024 * str2uint(&SERVER_CONFIGURATION["send_kernel_buffer_size"]);
		accept_client_params.resize_send_kernel_buffer = true;
	}

	struct epoll_event triggered_event;
	memset(&triggered_event, 0, sizeof(triggered_event)); //to suppress valgrind warnings
	
	while(true)
	{
		int epoll_result = epoll_wait(params.server_epoll, &triggered_event, 1, -1);

		if(epoll_result == -1)
		{
			if(errno != EINTR)
			{
				SERVER_ERROR_LOG_stdlib_err("Unable to listen to epoll!");
				return -1;
			}
		}

		else if(epoll_result > 0)
		{
			if(triggered_event.data.fd == params.http_listener)
			{
				if(triggered_event.events & EPOLLIN)
				{
					if(accept_new_client(accept_client_params) == -1)
					{
						return -1;
					}
				}

				else
				{
					SERVER_LOG_WRITE_ERROR.lock();
					SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING),true); 
                    SERVER_LOG_WRITE(" Unknown event occured in the listener epoll!\nEvent: 0x",true);
					SERVER_LOG_WRITE(std::hex,true);
					SERVER_LOG_WRITE(triggered_event.events,true);
					SERVER_LOG_WRITE("\n\n",true);
					SERVER_LOG_WRITE(std::dec,true);
					SERVER_LOG_WRITE_ERROR.unlock();
				}
			}

			else
			{
				break;
			}
		}
	}
	
	return 0;
}

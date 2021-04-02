#include <cstdlib>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "helper_functions.h"
#include "server_config.h"
#include "server_journal.h"
#include "https_listener.h"
#include "http_worker.h"

int get_openssl_error_callback(const char *str, size_t len, void *u)
{
	SERVER_JOURNAL_WRITE(str,true);
	SERVER_JOURNAL_WRITE("\n",true);
	return 0;
}


void https_listener_main(int SERVER_CLOSE_TRIGGER)
{
	SSL_load_error_strings();
	OpenSSL_add_ssl_algorithms();

	SSL_CTX* openssl_ctx;
	const SSL_METHOD* openssl_method = SSLv23_server_method();

	openssl_ctx = SSL_CTX_new(openssl_method);
	if (openssl_ctx == NULL)
	{
		SERVER_ERROR_JOURNAL_openssl_err("Unable to create SSL context!");
		exit(-1);
	}

	if (SSL_CTX_use_certificate_file(openssl_ctx,SERVER_CONFIGURATION["ssl_cert_file"].c_str(), SSL_FILETYPE_PEM) <= 0)
	{
		SERVER_JOURNAL_WRITE_ERROR.lock();
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
		SERVER_JOURNAL_WRITE(" Unable to load SSL certificate ( ",true);
		SERVER_JOURNAL_WRITE(SERVER_CONFIGURATION["ssl_cert_file"],true);
		SERVER_JOURNAL_WRITE(" )\n--OPENSSL ERRORS--\n",true);
		ERR_print_errors_cb(get_openssl_error_callback,0);
		SERVER_JOURNAL_WRITE("--OPENSSL ERRORS--\n\n",true);
		SERVER_JOURNAL_WRITE_ERROR.unlock();
		exit(-1);
	}

	if (SSL_CTX_use_PrivateKey_file(openssl_ctx,SERVER_CONFIGURATION["ssl_key_file"].c_str(), SSL_FILETYPE_PEM) <= 0 )
	{
		SERVER_JOURNAL_WRITE_ERROR.lock();
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
		SERVER_JOURNAL_WRITE(" Unable to load SSL private key ( ",true);
		SERVER_JOURNAL_WRITE(SERVER_CONFIGURATION["ssl_key_file"],true);
		SERVER_JOURNAL_WRITE(" )\n--OPENSSL ERRORS--\n",true);
		ERR_print_errors_cb(get_openssl_error_callback,0);
		SERVER_JOURNAL_WRITE("--OPENSSL ERRORS--\n\n",true);
		SERVER_JOURNAL_WRITE_ERROR.unlock();
		exit(-1);
	}

	int ip_addr_version = (SERVER_CONFIGURATION["ip_version"] == std::string("4")) ? AF_INET : AF_INET6;
	int HTTPS_LISTENER = socket(ip_addr_version,SOCK_STREAM,IPPROTO_TCP);

	if(HTTPS_LISTENER == -1)
	{
		SERVER_JOURNAL_WRITE_ERROR.lock();
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
		SERVER_JOURNAL_WRITE(" Unable to create https listener socket!\nError code: 0x",true);
		SERVER_JOURNAL_WRITE(std::hex,true);
		SERVER_JOURNAL_WRITE(errno,true);
		SERVER_JOURNAL_WRITE("\n",true);
		SERVER_JOURNAL_WRITE(strerror(errno),true);
		SERVER_JOURNAL_WRITE("\n\n",true);
		SERVER_JOURNAL_WRITE(std::dec,true);
		SERVER_JOURNAL_WRITE_ERROR.unlock();
		exit(-1);
	}

	if (set_socket_nonblock(HTTPS_LISTENER) == -1)
	{
		SERVER_JOURNAL_WRITE_ERROR.lock();
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
		SERVER_JOURNAL_WRITE(" Unable to put https listener socket in nonblocking mode!\nError code: 0x",true);
		SERVER_JOURNAL_WRITE(std::hex,true);
		SERVER_JOURNAL_WRITE(errno,true);
		SERVER_JOURNAL_WRITE("\n",true);
		SERVER_JOURNAL_WRITE(strerror(errno),true);
		SERVER_JOURNAL_WRITE("\n\n",true);
		SERVER_JOURNAL_WRITE(std::dec,true);
		SERVER_JOURNAL_WRITE_ERROR.unlock();
		exit(-1);
	}

	if(ip_addr_version == AF_INET6)
	{
		
		if(is_server_config_variable_true("ipv6_dual_stack"))
		{
			int sock_opt = 0;     
			if(setsockopt(HTTPS_LISTENER, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&sock_opt, sizeof(sock_opt)) == -1)
			{
				SERVER_JOURNAL_WRITE_ERROR.lock();
				SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
				SERVER_JOURNAL_WRITE(" Unable to enable ipv6 dual stack!\nError code: 0x",true); SERVER_JOURNAL_WRITE(std::hex,true);
				SERVER_JOURNAL_WRITE(errno,true);
				SERVER_JOURNAL_WRITE("\n",true);
				SERVER_JOURNAL_WRITE(strerror(errno),true);
				SERVER_JOURNAL_WRITE("\n\n",true);
				SERVER_JOURNAL_WRITE(std::dec,true);
				SERVER_JOURNAL_WRITE_ERROR.unlock();
			}
		}

		if(is_server_config_variable_false("ipv6_dual_stack"))
		{
			int sock_opt = 1;     
			if(setsockopt(HTTPS_LISTENER, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&sock_opt, sizeof(sock_opt)) == -1)
			{
				SERVER_JOURNAL_WRITE_ERROR.lock();
				SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
				SERVER_JOURNAL_WRITE(" Unable to disable ipv6 dual stack!\nError code: 0x",true);
				SERVER_JOURNAL_WRITE(std::hex,true);
				SERVER_JOURNAL_WRITE(errno,true);
				SERVER_JOURNAL_WRITE("\n",true);
				SERVER_JOURNAL_WRITE(strerror(errno),true);
				SERVER_JOURNAL_WRITE("\n\n",true);
				SERVER_JOURNAL_WRITE(std::dec,true);
				SERVER_JOURNAL_WRITE_ERROR.unlock();
			}
		}
		
	}



	

	if(is_server_config_variable_true("reuse_addr"))
	{
		int enable = 1;
		if (setsockopt(HTTPS_LISTENER, SOL_SOCKET, SO_REUSEADDR,&enable,sizeof(int)) == -1)
		{
				SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
				SERVER_JOURNAL_WRITE(" Unable to enable reuse_addr option on https listener socket!\nError code: 0x",true); SERVER_JOURNAL_WRITE(std::hex,true);
				SERVER_JOURNAL_WRITE(errno,true);
				SERVER_JOURNAL_WRITE("\n",true);
				SERVER_JOURNAL_WRITE(strerror(errno),true);
				SERVER_JOURNAL_WRITE("\n\n",true);
				SERVER_JOURNAL_WRITE(std::dec,true);
		}

	}

	if(is_server_config_variable_false("reuse_addr"))
	{
		int enable = 0;
		if (setsockopt(HTTPS_LISTENER, SOL_SOCKET, SO_REUSEADDR, &enable,sizeof(int)) == -1)
		{
				SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
				SERVER_JOURNAL_WRITE(" Unable to disable reuse_addr option on https listener socket!\nError code: 0x",true);
				SERVER_JOURNAL_WRITE(std::hex,true);
				SERVER_JOURNAL_WRITE(errno,true);
				SERVER_JOURNAL_WRITE("\n",true);
				SERVER_JOURNAL_WRITE(strerror(errno),true);
				SERVER_JOURNAL_WRITE("\n\n",true);
				SERVER_JOURNAL_WRITE(std::dec,true);
		}
	}

	



	struct sockaddr_in6 HTTPS_LISTENER_ADDR;
	memset(&HTTPS_LISTENER_ADDR,0,sizeof(struct sockaddr_in6));

	struct sockaddr_in* HTTPS_LISTENER_ADDR4=(struct sockaddr_in*)&HTTPS_LISTENER_ADDR;
	struct sockaddr_in6* HTTPS_LISTENER_ADDR6=&HTTPS_LISTENER_ADDR;

	HTTPS_LISTENER_ADDR.sin6_family=ip_addr_version;

	inet_pton(ip_addr_version,SERVER_CONFIGURATION["ip_addr"].c_str(),((ip_addr_version == AF_INET6) ? (void*)&HTTPS_LISTENER_ADDR6->sin6_addr : (void*)&HTTPS_LISTENER_ADDR4->sin_addr));

	if(ip_addr_version == AF_INET6)
		HTTPS_LISTENER_ADDR6->sin6_port = htons(str2uint(&SERVER_CONFIGURATION["listen_https_port"]));
		
	else
		HTTPS_LISTENER_ADDR4->sin_port = htons(str2uint(&SERVER_CONFIGURATION["listen_https_port"]));

	if(bind(HTTPS_LISTENER,(struct sockaddr*)&HTTPS_LISTENER_ADDR,((ip_addr_version == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in))) == -1)
	{
		SERVER_JOURNAL_WRITE_ERROR.lock();
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
		SERVER_JOURNAL_WRITE(" Unable to bind https listener socket!\nError code: 0x",true);
		SERVER_JOURNAL_WRITE(std::hex,true);
		SERVER_JOURNAL_WRITE(errno,true);
		SERVER_JOURNAL_WRITE("\n",true);
		SERVER_JOURNAL_WRITE(strerror(errno),true);
		SERVER_JOURNAL_WRITE("\n\n",true);
		SERVER_JOURNAL_WRITE(std::dec,true);
		SERVER_JOURNAL_WRITE_ERROR.unlock();
		exit(-1);
	}

	if(listen(HTTPS_LISTENER,SOMAXCONN) == -1)
	{
		SERVER_JOURNAL_WRITE_ERROR.lock();
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
		SERVER_JOURNAL_WRITE(" Unable to put https listener socket in listening state!\nError code: 0x",true);
		SERVER_JOURNAL_WRITE(std::hex,true);
		SERVER_JOURNAL_WRITE(errno,true);
		SERVER_JOURNAL_WRITE("\n",true);
		SERVER_JOURNAL_WRITE(strerror(errno),true);
		SERVER_JOURNAL_WRITE("\n\n",true);
		SERVER_JOURNAL_WRITE(std::dec,true);
		SERVER_JOURNAL_WRITE_ERROR.unlock();
		exit(-1);
	}


	int SERVER_HTTPS_EPOLL = epoll_create1(EPOLL_CLOEXEC);
	if(SERVER_HTTPS_EPOLL == -1)
	{
		SERVER_JOURNAL_WRITE_ERROR.lock();
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
		SERVER_JOURNAL_WRITE(" Unable to create https server epoll!\nError code: 0x",true);
		SERVER_JOURNAL_WRITE(std::hex,true);
		SERVER_JOURNAL_WRITE(errno,true);
		SERVER_JOURNAL_WRITE("\n",true);
		SERVER_JOURNAL_WRITE(strerror(errno),true);
		SERVER_JOURNAL_WRITE("\n\n",true);
		SERVER_JOURNAL_WRITE(std::dec,true);
		SERVER_JOURNAL_WRITE_ERROR.unlock();
		exit(-1);
	}


	struct epoll_event epoll_config;
	epoll_config.events = EPOLLIN | EPOLLET;
	epoll_config.data.fd = HTTPS_LISTENER;

	if(epoll_ctl(SERVER_HTTPS_EPOLL,EPOLL_CTL_ADD,HTTPS_LISTENER,&epoll_config) == -1)
	{
		SERVER_JOURNAL_WRITE_ERROR.lock();
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
		SERVER_JOURNAL_WRITE(" Unable to add https listener socket to epoll!\nError code: 0x",true);
		SERVER_JOURNAL_WRITE(std::hex,true);
		SERVER_JOURNAL_WRITE(errno,true);
		SERVER_JOURNAL_WRITE("\n",true);
		SERVER_JOURNAL_WRITE(strerror(errno),true);
		SERVER_JOURNAL_WRITE("\n\n",true);
		SERVER_JOURNAL_WRITE(std::dec,true);
		SERVER_JOURNAL_WRITE_ERROR.unlock();
		exit(-1);
	}

	epoll_config.events = EPOLLIN | EPOLLET;
	epoll_config.data.fd = SERVER_CLOSE_TRIGGER;

	if(epoll_ctl(SERVER_HTTPS_EPOLL,EPOLL_CTL_ADD,SERVER_CLOSE_TRIGGER,&epoll_config) == -1)
	{
		SERVER_JOURNAL_WRITE_ERROR.lock();
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
		SERVER_JOURNAL_WRITE(" Unable to add server close trigger to epoll!\nError code: 0x",true);
		SERVER_JOURNAL_WRITE(std::hex,true);
		SERVER_JOURNAL_WRITE(errno,true);
		SERVER_JOURNAL_WRITE("\n",true);
		SERVER_JOURNAL_WRITE(strerror(errno),true);
		SERVER_JOURNAL_WRITE("\n\n",true);
		SERVER_JOURNAL_WRITE(std::dec,true);
		SERVER_JOURNAL_WRITE_ERROR.unlock();
		exit(-1);
	}

	unsigned int max_connections = str2uint(&SERVER_CONFIGURATION["max_connections"]);

	struct sockaddr incoming_addr;
	socklen_t incoming_addr_size = (ip_addr_version == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
	uint16_t incoming_port;
	char incoming_addr_str[INET6_ADDRSTRLEN];

	struct epoll_event triggered_event;

	while(true)
	{
		int epoll_result = epoll_wait(SERVER_HTTPS_EPOLL,&triggered_event,1,-1);

		if(epoll_result == -1)
		{
			if(errno != EINTR)
			{
				SERVER_JOURNAL_WRITE_ERROR.lock();
				SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
				SERVER_JOURNAL_WRITE(" Unable to listen to epoll!\nError code: 0x",true);
				SERVER_JOURNAL_WRITE(std::hex,true);
				SERVER_JOURNAL_WRITE(errno,true);
				SERVER_JOURNAL_WRITE("\n",true);
				SERVER_JOURNAL_WRITE(strerror(errno),true);
				SERVER_JOURNAL_WRITE("\n\n",true); SERVER_JOURNAL_WRITE(std::dec,true);
				SERVER_JOURNAL_WRITE_ERROR.unlock();
				exit(-1);
			}

		}

		else if(epoll_result > 0)
		{
			if(triggered_event.data.fd == HTTPS_LISTENER)
			{
				if(triggered_event.events & EPOLLIN)
				{
					while(true)
					{
						int new_http_client = accept(HTTPS_LISTENER,&incoming_addr,&incoming_addr_size);

						if(new_http_client == -1)
						{
							if(errno == EINTR)
								continue;
								
							if(errno == EAGAIN or errno == EWOULDBLOCK)
								break;

							SERVER_JOURNAL_WRITE_ERROR.lock();
							SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
							SERVER_JOURNAL_WRITE(" Unable to accept incoming connection!\nError code: 0x",true);
							SERVER_JOURNAL_WRITE(std::hex,true);
							SERVER_JOURNAL_WRITE(errno,true);
							SERVER_JOURNAL_WRITE("\n",true);
							SERVER_JOURNAL_WRITE(strerror(errno),true);
							SERVER_JOURNAL_WRITE("\n\n",true);
							SERVER_JOURNAL_WRITE(std::dec,true);
							SERVER_JOURNAL_WRITE_ERROR.unlock();
							exit(-1);
						}


						if(total_http_connections >= max_connections)
						{
							SERVER_ERROR_JOURNAL_conn_exceeded();
							close(new_http_client);
							continue;
						}
						
						total_http_connections++;


						if (set_socket_nonblock(new_http_client) == -1)
						{
							SERVER_JOURNAL_WRITE_ERROR.lock();
							SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
							SERVER_JOURNAL_WRITE("Unable to put https client in nonblocking mode!\nError code: 0x",true);
							SERVER_JOURNAL_WRITE(std::hex,true);
							SERVER_JOURNAL_WRITE(errno,true);
							SERVER_JOURNAL_WRITE("\n",true);
							SERVER_JOURNAL_WRITE(strerror(errno),true);
							SERVER_JOURNAL_WRITE("\n\n",true);
							SERVER_JOURNAL_WRITE(std::dec,true);
							SERVER_JOURNAL_WRITE_ERROR.unlock();
							exit(-1);
						}

						struct sockaddr_in* incoming_addr4 = (sockaddr_in*)&incoming_addr;
						struct sockaddr_in6* incoming_addr6 = (sockaddr_in6*)&incoming_addr;

						if(inet_ntop(incoming_addr.sa_family,((incoming_addr.sa_family == AF_INET6) ? (void*)&incoming_addr6->sin6_addr : (void*)&incoming_addr4->sin_addr),
							     incoming_addr_str,incoming_addr_size) == NULL)
						{
							SERVER_JOURNAL_WRITE_ERROR.lock();
							SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
							SERVER_JOURNAL_WRITE(" Unable to read remote socket address!\nError code: 0x",true);
							SERVER_JOURNAL_WRITE(std::hex,true);
							SERVER_JOURNAL_WRITE(errno,true);
							SERVER_JOURNAL_WRITE("\n",true);
							SERVER_JOURNAL_WRITE(strerror(errno),true);
							SERVER_JOURNAL_WRITE("\n\n",true);
							SERVER_JOURNAL_WRITE(std::dec,true);
							SERVER_JOURNAL_WRITE_ERROR.unlock();
							exit(-1);
						}


						incoming_port=ntohs(((incoming_addr.sa_family == AF_INET6) ? incoming_addr6->sin6_port : incoming_addr4->sin_port));

						worker_add_client(new_http_client,incoming_addr_str,&SERVER_CONFIGURATION["ip_addr"],incoming_port,str2uint(&SERVER_CONFIGURATION["listen_https_port"]),
								  incoming_addr.sa_family,true,openssl_ctx);
					}
				}

				else
				{
					SERVER_JOURNAL_WRITE_ERROR.lock();
					SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
					SERVER_JOURNAL_WRITE(" Unknown event occured on https listener epoll!\nEvent: 0x",true);
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

	close(HTTPS_LISTENER);
	close(SERVER_HTTPS_EPOLL);
	SSL_CTX_free(openssl_ctx);
	EVP_cleanup();

}

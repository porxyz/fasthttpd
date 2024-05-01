#ifndef __server_listener_API_incl__
#define __server_listener_API_incl__

#ifndef DISABLE_HTTPS
#include <openssl/ssl.h>
#endif

int init_server_listener_socket(bool https = false);
int init_server_listener_epoll(int listener_socket,int close_trigger);

typedef struct 
{
    int server_epoll;
	int http_listener;
	bool https;

	#ifndef DISABLE_HTTPS
	SSL_CTX* openssl_ctx;
	#endif
} server_listener_parameters;

int run_server_listener_loop(server_listener_parameters& params);

#endif

#ifndef __server_listener_API_incl__
#define __server_listener_API_incl__

#ifndef DISABLE_HTTPS
#include <openssl/ssl.h>
#endif

int init_server_listener_socket(bool https = false);
int init_server_listener_epoll(int listener_socket,int close_trigger);

#ifndef DISABLE_HTTPS
int run_server_listener_loop(int server_epoll, int http_listener, bool https = false, SSL_CTX* openssl_ctx = NULL);
#else
int run_server_listener_loop(int server_epoll, int http_listener, bool https = false);
#endif


#endif

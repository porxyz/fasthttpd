#ifndef __https_listener_incl__
#define __https_listener_incl__

void https_listener_main(int);

int get_openssl_error_callback(const char *str, size_t len, void *u);

#endif

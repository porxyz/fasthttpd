#include <cstring>

#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "helper_functions.h"
#include "server_config.h"
#include "server_log.h"
#include "https_listener.h"
#include "server_listener.h"
#include "http_worker/http_worker.h"

int get_openssl_error_callback(const char *str, size_t len, void *u)
{
	SERVER_LOG_WRITE(str,true);
	SERVER_LOG_WRITE("\n",true);
	return 0;
}

static int openssl_ALPN_select_callback(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned int inlen, void *arg)
{
	if(!in or !inlen)
	{
		return SSL_TLSEXT_ERR_ALERT_FATAL;
	}
	
	void* ext_http_1 = 0;
	void* ext_http_2 = 0;
	
	unsigned char* ALPN_input = (unsigned char*)in;
	unsigned int ALPN_offset = 0;
	
	while(ALPN_offset < inlen)
	{
		unsigned char current_ext_len = ALPN_input[ALPN_offset];
		ALPN_offset++;
		
		if(current_ext_len + ALPN_offset <= inlen)
		{
			if(current_ext_len == 2 and memcmp("h2", ALPN_input + ALPN_offset, 2) == 0)
			{
				ext_http_2 = ALPN_input + ALPN_offset;
			}
			else if(current_ext_len == 8 and memcmp("http/1.1", ALPN_input + ALPN_offset, 8) == 0)
			{
				ext_http_1 = ALPN_input + ALPN_offset;
			}
		}
		else //malformed ALPN input
		{
			return SSL_TLSEXT_ERR_ALERT_FATAL;
		}
			
		ALPN_offset+=current_ext_len;
	}
	
	if(ext_http_2)
	{
		out[0] = (unsigned char*)ext_http_2;
		outlen[0] = 2;
	}
	else if(ext_http_1)
	{
		out[0] = (unsigned char*)ext_http_1;
		outlen[0] = 8;
	}
	else //no protocol is supported
	{
		return SSL_TLSEXT_ERR_ALERT_FATAL;
	}
	
	return SSL_TLSEXT_ERR_OK;
}

void https_listener_main(int SERVER_CLOSE_TRIGGER)
{
	SSL_load_error_strings();
	OpenSSL_add_ssl_algorithms();

	const SSL_METHOD* openssl_method = TLS_server_method();

	SSL_CTX* openssl_ctx = SSL_CTX_new(openssl_method);
	if (openssl_ctx == NULL)
	{
		SERVER_ERROR_LOG_openssl_err("Unable to create SSL context!");
		exit(-1);
	}

	if (SSL_CTX_use_certificate_file(openssl_ctx,SERVER_CONFIGURATION["ssl_cert_file"].c_str(), SSL_FILETYPE_PEM) <= 0)
	{
		SERVER_LOG_WRITE_ERROR.lock();
		SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING),true); 
		SERVER_LOG_WRITE(" Unable to load SSL certificate ( ",true);
		SERVER_LOG_WRITE(SERVER_CONFIGURATION["ssl_cert_file"],true);
		SERVER_LOG_WRITE(" )\n--OPENSSL ERRORS--\n",true);
		ERR_print_errors_cb(get_openssl_error_callback,0);
		SERVER_LOG_WRITE("--OPENSSL ERRORS--\n\n",true);
		SERVER_LOG_WRITE_ERROR.unlock();

		exit(-1);
	}

	if (SSL_CTX_use_PrivateKey_file(openssl_ctx,SERVER_CONFIGURATION["ssl_key_file"].c_str(), SSL_FILETYPE_PEM) <= 0 )
	{
		SERVER_LOG_WRITE_ERROR.lock();
		SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING),true); 
		SERVER_LOG_WRITE(" Unable to load SSL private key ( ",true);
		SERVER_LOG_WRITE(SERVER_CONFIGURATION["ssl_key_file"],true);
		SERVER_LOG_WRITE(" )\n--OPENSSL ERRORS--\n",true);
		ERR_print_errors_cb(get_openssl_error_callback,0);
		SERVER_LOG_WRITE("--OPENSSL ERRORS--\n\n",true);
		SERVER_LOG_WRITE_ERROR.unlock();

		exit(-1);
	}

	//advertised TLS ALPN extensions
    unsigned char ALPN_vec[] = {8, 'h', 't', 't', 'p', '/', '1', '.', '1', 3, 'h', '2', 'c', 2, 'h', '2'};
 	unsigned int ALPN_vec_len = sizeof(ALPN_vec);

	if(SSL_CTX_set_alpn_protos(openssl_ctx, ALPN_vec, ALPN_vec_len) != 0)
 	{
		SERVER_ERROR_LOG_openssl_err("Unable to advertise ALPN extensions!");
 	}

	SSL_CTX_set_alpn_select_cb(openssl_ctx, openssl_ALPN_select_callback, NULL);
	
	int HTTPS_LISTENER = init_server_listener_socket(true);
	if(HTTPS_LISTENER == -1)
	{
		exit(-1);
	}

	int SERVER_HTTPS_EPOLL = init_server_listener_epoll(HTTPS_LISTENER, SERVER_CLOSE_TRIGGER);
	if(SERVER_HTTPS_EPOLL == -1)
	{
		exit(-1);
	}

	server_listener_parameters listener_params;
	listener_params.https = true;
	listener_params.http_listener = HTTPS_LISTENER;
	listener_params.server_epoll = SERVER_HTTPS_EPOLL;
	listener_params.openssl_ctx = openssl_ctx;

	if(run_server_listener_loop(listener_params) == -1)
	{
		exit(-1);
	}

	close(HTTPS_LISTENER);
	close(SERVER_HTTPS_EPOLL);
	SSL_CTX_free(openssl_ctx);
	EVP_cleanup();
}



#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "helper_functions.h"
#include "server_config.h"
#include "server_journal.h"
#include "https_listener.h"
#include "server_listener.h"

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

	const SSL_METHOD* openssl_method = SSLv23_server_method();

	SSL_CTX* openssl_ctx = SSL_CTX_new(openssl_method);
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

	
	int HTTPS_LISTENER = init_server_listener_socket(true);
	if(HTTPS_LISTENER == -1)
		exit(-1);

	int SERVER_HTTPS_EPOLL = init_server_listener_epoll(HTTPS_LISTENER,SERVER_CLOSE_TRIGGER);
	if(SERVER_HTTPS_EPOLL == -1)
		exit(-1);
		
	if(run_server_listener_loop(SERVER_HTTPS_EPOLL,HTTPS_LISTENER,true,openssl_ctx) == -1)
		exit(-1);


	close(HTTPS_LISTENER);
	close(SERVER_HTTPS_EPOLL);
	SSL_CTX_free(openssl_ctx);
	EVP_cleanup();

}



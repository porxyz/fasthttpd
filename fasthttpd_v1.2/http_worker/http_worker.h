#ifndef __http_worker_incl__
#define __http_worker_incl__

#include <sys/socket.h>

#include <thread>
#include <mutex>
#include <atomic>

#ifndef DISABLE_HTTPS
#include <openssl/ssl.h>
#endif

#ifndef NO_MOD_MYSQL
#include "../mod_mysql.h"
#endif

#include "http1_connection_processor.h"
#include "http2_connection_processor.h"

#include "http2_frame_processor.h"
#include "http2_stream_processor.h"

#include "request_processor.h"

struct GENERIC_HTTP_CONNECTION
{
	int http_version;
	uint8_t state;
	bool https;

	std::string remote_addr;
	uint16_t remote_port;
	std::string server_addr;
	uint16_t server_port;

	int ip_addr_version;

	int client_sock;
	#ifndef DISABLE_HTTPS
	SSL* ssl_wrapper;
	#endif

	struct timespec last_action;
	int milisecond_timeout;

	void* raw_connection;
};


struct HTTP_WORKER_NODE
{
	std::thread* worker_thread;
	std::unordered_map<int, struct GENERIC_HTTP_CONNECTION> connections;
	std::mutex* connections_mutex;
	int worker_epoll;
	char* recv_buffer;
	
	#ifndef NO_MOD_MYSQL
	mysql_connection* mysql_db_handle;
	#endif

};

extern bool is_server_load_balancer_fair;
extern std::atomic<size_t> total_http_connections;
extern std::vector<struct HTTP_WORKER_NODE> http_workers;
extern std::vector<std::atomic<int>> connections_per_worker;

typedef struct
{
	int client_sock;
	const char *remote_addr;
	const char *server_addr;
	uint16_t remote_port; 
	uint16_t server_port;
	sa_family_t ip_addr_version; 
	bool https;
	#ifndef DISABLE_HTTPS
	SSL_CTX *openssl_ctx;
	#endif
} HTTP_Worker_Add_Client_Parameters;


void HTTP_Worker_Add_Client(HTTP_Worker_Add_Client_Parameters& params);
void HTTP_Workers_Init(int close_trigger);
void HTTP_Workers_Join();

void HTTP_Worker_Init_Aux_Modules(int worker_id);
void HTTP_Worker_Free_Aux_Modules(int worker_id);

int Network_Read_Bytes(struct GENERIC_HTTP_CONNECTION* conn, void *buffer, size_t len);
int Network_Write_Bytes(struct GENERIC_HTTP_CONNECTION* conn, void *buffer, size_t len);

void Generic_Connection_Delete(const int worker_id, struct GENERIC_HTTP_CONNECTION* conn, bool lock_mutex = true);

#endif


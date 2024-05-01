#ifndef __http_worker_incl__
#define __http_worker_incl__  

#define HTTP_STATE_INIT 0
#define HTTP_STATE_SSL_INIT 255
#define HTTP_STATE_FILE_BOUND 1
#define HTTP_STATE_CONTENT_BOUND 2
#define HTTP_STATE_CUSTOM_BOUND 3

#define HTTP_VERSION_UNDEFINED 0
#define HTTP_VERSION_1 1
#define HTTP_VERSION_1_1 2
#define HTTP_VERSION_2 3

#define HTTP_METHOD_UNDEFINED 0
#define HTTP_METHOD_GET 1
#define HTTP_METHOD_POST 2
#define HTTP_METHOD_HEAD 3
#define HTTP_METHOD_PUT 4
#define HTTP_METHOD_DELETE 5
#define HTTP_METHOD_OPTIONS 6
#define HTTP_METHOD_CONNECT 7
#define HTTP_METHOD_TRACE 8
#define HTTP_METHOD_PATCH 9


#define HTTP_POST_TYPE_UNDEFINED 0
#define HTTP_POST_APPLICATION_X_WWW_FORM_URLENCODED 1
#define HTTP_POST_MULTIPART_FORM_DATA 2
#define HTTP_POST_RAW_DATA 3

#include <unordered_map> 
#include <list> 
#include <string> 
#include <thread>
#include <mutex> 
#include <atomic> 
#include <vector> 
#include <ctime>

#ifndef DISABLE_HTTPS
#include <openssl/ssl.h>
#endif

#ifndef NO_MOD_MYSQL
#include "mod_mysql.h"
#endif



struct HTTP_POST_FILE
{
	std::string data;
	std::string type;
};

struct HTTP_COOKIE
{
	std::string name;
	std::string value;
	time_t expires;
	unsigned int max_age;
	std::string same_site;
	std::string domain;
	std::string path;
	bool secure;
	bool http_only;
};

struct http_request
{
	int request_method;
	int POST_type;
	std::unordered_map <std::string , std::string> request_headers;
	std::string URI_path;
	std::unordered_map <std::string,std::string> URI_query;
	std::unordered_map <std::string,std::string>* POST_query;
	std::unordered_map <std::string,std::string>* COOKIES;
	std::unordered_map<std::string, std::unordered_map<std::string,struct HTTP_POST_FILE>>* POST_files;
};

struct http_response
{
	int response_code;
	std::unordered_map <std::string , std::string> response_headers;
	std::string response_body;
	std::vector<struct HTTP_COOKIE> *COOKIES;
};


struct http_connection
{
	int client_socket;
	int requested_fd;
	uint8_t state;
	struct http_request request;
	struct http_response response;
	std::string recv_buffer;
	std::string send_buffer;
	size_t send_buffer_offset;
	size_t requested_fd_offset;
	size_t requested_fd_stop;
	std::string remote_addr;
	uint16_t remote_port;
	std::string server_addr;
	uint16_t server_port;
	int ip_addr_version;
	int http_version;
	bool https;

	#ifndef DISABLE_HTTPS
	SSL* ssl_connection;
	#endif

	struct timespec last_action;
	int milisecond_timeout;
	std::list<struct http_connection>::iterator self_reference;
};


struct http_worker_node
{
	std::thread* worker_thread;
	std::list<struct http_connection> connections;
	std::mutex* connections_mutex;
	int worker_epoll;

	#ifndef NO_MOD_MYSQL
	mysql_connection* mysql_db_handle;
	#endif
};



extern std::atomic<size_t> total_http_connections;
extern std::vector<struct http_worker_node> http_workers;

void worker_add_client(int new_client,const char* remote_addr,std::string* server_addr,int16_t remote_port,int16_t server_port,int ip_addr_version,bool https = false
			#ifndef DISABLE_HTTPS
			, SSL_CTX* openssl_ctx = NULL
			#endif
			);

void init_workers(int close_trigger);

void wait_for_workers_to_exit();

int detect_http_version(const std::string* raw_request);

int detect_http_method(const std::string* raw_request);

bool parse_http_URI(const std::string* raw_request,std::string* URI,std::unordered_map<std::string , std::string>* URI_query_params);

bool parse_http_request_headers(const std::string* raw_request,std::unordered_map<std::string , std::string>* request_headers);

void parse_http_request_cookies(std::list<struct http_connection>::iterator* current_connection);

bool decode_http_content_range(const std::string* content_range,int64_t* offset_start,int64_t* offset_stop);

bool parse_http_request_POST_body(std::list<struct http_connection>::iterator* current_connection,uint8_t parse_type);

bool parse_multipart_form_data(const std::string* raw_request,const std::string* boundary,std::unordered_map<std::string,std::string> *POST_query,
std::unordered_map<std::string,std::unordered_map<std::string,struct HTTP_POST_FILE>> *POST_files,unsigned int POST_arg_limit,
unsigned int POST_files_limit,bool* POST_arg_limit_exceeded,bool* POST_files_limit_exceeded,bool continue_when_limit_exceeded);

bool is_POST_request_complete(std::list<struct http_connection>::iterator* http_connection);

std::string encode_http_content_range(int64_t offset_start,int64_t offset_stop,int64_t file_size);

void init_http_cookie(struct HTTP_COOKIE* c,const char* name,const char* value);

void init_http_cookie(struct HTTP_COOKIE* c,const std::string* name,const std::string* value);

void generate_cookie_header(std::string* raw_request,const struct HTTP_COOKIE* c);

void set_http_error_page(std::list<struct http_connection>::iterator* current_connection,int error_code,std::string reason = std::string());

void generate_http_output(std::list<struct http_connection>::iterator* current_connection);

void delete_http_connection(size_t worker_id,std::list<struct http_connection>::iterator* current_connection,bool allow_keepalive=false,bool lock_mutex=true);

bool send_all(std::list<struct http_connection>::iterator* current_connection);

bool recv_all(std::list<struct http_connection>::iterator* current_connection,char* buff,size_t buff_size,uint64_t max_request_limit,bool* limit_exceeded);

void init_worker_aux_modules(int worker_id);

void free_worker_aux_modules(int worker_id);

#endif





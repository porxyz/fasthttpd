#ifndef __http_core__inc
#define __http_core__inc

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

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

#define HTTP_STATE_SSL_INIT 0
#define HTTP_STATE_WAIT_PATH 1
#define HTTP_STATE_WAIT_HEADERS 2
#define HTTP_STATE_WAIT_BODY 3
#define HTTP_STATE_PROCESSING 4
#define HTTP_STATE_FILE_BOUND 5
#define HTTP_STATE_CONTENT_BOUND 6
#define HTTP_STATE_CUSTOM_BOUND 7


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

struct HTTP_REQUEST
{
	int method;
	int POST_type;
	std::unordered_map <std::string , std::string> headers;
	std::string URI_path;
	std::unordered_map <std::string,std::string> URI_query;
	std::unordered_map <std::string,std::string>* POST_query;
	std::unordered_map <std::string,std::string>* COOKIES;
	std::unordered_map <std::string, std::unordered_map<std::string, struct HTTP_POST_FILE>>* POST_files;
};

struct HTTP_RESPONSE
{
	int code;
	std::unordered_map <std::string , std::string> headers;
	std::string body;
	std::vector<struct HTTP_COOKIE> *COOKIES;
};

struct HTTP_FILE_TRANSFER
{
	int file_descriptor;
	int64_t file_offset;
	int64_t stop_offset;
};

struct HTTP_PARSER_HELPER
{
	size_t headers_start_offset;
	size_t body_start_offset;
	uint64_t content_length;
};

struct HTTP1_CONNECTION
{
	std::string recv_buffer;

	std::string send_buffer;
	size_t send_buffer_offset;
	
	struct HTTP_REQUEST request;
	struct HTTP_RESPONSE response;

	struct HTTP_FILE_TRANSFER file_transfer;

	struct HTTP_PARSER_HELPER parser_helper;
};

#endif


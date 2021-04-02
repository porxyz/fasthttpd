#ifndef __custom_bound_incl__
#define __custom_bound_incl__

#include <list>
#include <string>

#include "helper_functions.h"
#include "http_worker.h"

#define ANY_HOSTNAME_PATH NULL

#define echo(X) http_connection->response.response_body.append(X)

#define HTTP_GET_ARG(X) http_connection->request.URI_query[X]
#define HTTP_GET_ARGC (http_connection->request.URI_query.size())
#define HTTP_GET_EXISTS(X) (http_connection->request.URI_query.find(X) != http_connection->request.URI_query.end())

#define HTTP_POST_ARG(X) http_connection->request.POST_query[X]
#define HTTP_POST_ARGC (http_connection->request.POST_query.size())
#define HTTP_POST_ARG_EXISTS(X) (http_connection->request.POST_query.find(X) != http_connection->request.POST_query.end())


typedef void (*http_function_t)(std::list<struct http_connection>::iterator &current_connection,size_t thread_id);

struct custom_bound_entry
{
	bool execute_only_when_loaded;
	http_function_t page_generator;
};

bool check_custom_bound_path(const std::string* filename,const std::string* host_path,struct custom_bound_entry* result);
void run_custom_page_generator(std::list<struct http_connection>::iterator* current_connection,size_t worker_id,struct custom_bound_entry* generator);
void add_custom_bound_path(http_function_t page_generator,const char* path,bool execute_only_when_loaded = true,const char* hostname = ANY_HOSTNAME_PATH);
void load_custom_bound_paths();

#endif
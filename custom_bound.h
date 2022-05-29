#define __custom_bound_incl__

#include <list>
#include <string>

#include "helper_functions.h"
#include "http_worker.h"

#define ANY_HOSTNAME_PATH NULL

#define echo(X) http_connection->response.response_body.append(X)

#define HTTP_GET_ARG(X) http_connection->request.URI_query[X]
#define HTTP_GET_ARGC (http_connection->request.URI_query.size())
#define HTTP_GET_ARG_EXISTS(X) (http_connection->request.URI_query.find(X) != http_connection->request.URI_query.end())

#define HTTP_POST_ARG(X) http_connection->request.POST_query->at(X)
#define HTTP_POST_ARGC (http_connection->request.POST_query) ? (http_connection->request.POST_query->size()) : 0
#define HTTP_POST_ARG_EXISTS(X) (http_connection->request.POST_query) ? (http_connection->request.POST_query->find(X) != http_connection->request.POST_query->end()) : 0

#define HTTP_COOKIE(X) http_connection->request.COOKIES->at(X)
#define HTTP_COOKIE_ARGC (http_connection->request.COOKIES) ? (http_connection->request.COOKIES->size()) : 0
#define HTTP_COOKIE_EXISTS(X) (http_connection->request.COOKIES) ? (http_connection->request.COOKIES->find(X) != http_connection->request.COOKIES->end()) : 0


typedef void (*http_function_t)(std::list<struct http_connection>::iterator &current_connection,size_t thread_id);

struct custom_bound_entry
{
	bool execute_only_when_loaded;
	http_function_t page_generator;
};

bool check_custom_bound_path(const std::string& filename,struct custom_bound_entry* result);
void run_custom_page_generator(std::list<struct http_connection>::iterator* current_connection,size_t worker_id,struct custom_bound_entry* generator);
void add_custom_bound_path(http_function_t page_generator,const char* path,const char* hostname = ANY_HOSTNAME_PATH,bool execute_only_when_loaded = true);
void load_custom_bound_paths();

#endif

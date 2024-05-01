#ifndef __custom_bound_incl__
#define __custom_bound_incl__

#include <list>
#include <string>

#include "http_worker/http_worker.h"
#include "http_worker/http_parser.h"
#include "helper_functions.h"

#define ANY_HOSTNAME_PATH NULL

#define echo(X) args.response->body.append(X)

#define HTTP_GET_ARG(X) args.request->URI_query->at(X)
#define HTTP_GET_ARGC (args.request->URI_query.size())
#define HTTP_GET_ARG_EXISTS(X) (args.request->URI_query.find(X) != args.request->URI_query.end())

#define HTTP_POST_ARG(X) args.request->POST_query->at(X)
#define HTTP_POST_ARGC ((args.request->POST_query) ? (args.request->POST_query->size()) : 0)
#define HTTP_POST_ARG_EXISTS(X) ((args.request->POST_query) ? (args.request->POST_query->find(X) != args.request->POST_query->end()) : 0)

#define HTTP_COOKIE(X) args.request->COOKIES->at(X)
#define HTTP_COOKIE_ARGC ((args.request->COOKIES) ? (args.request->COOKIES->size()) : 0)
#define HTTP_COOKIE_EXISTS(X) ((args.request->COOKIES) ? (args.request->COOKIES->find(X) != args.request->COOKIES->end()) : 0)

struct HTTP_CUSTOM_PAGE_HANDLER_ARGUMENTS
{
	int worker_id;
	struct GENERIC_HTTP_CONNECTION *conn;
	int stream_id;
	struct HTTP_REQUEST *request;
	struct HTTP_RESPONSE *response;
};

typedef int (*HTTP_CUSTOM_PAGE_HANDLER)(const struct HTTP_CUSTOM_PAGE_HANDLER_ARGUMENTS& args);

struct custom_bound_entry
{
	bool execute_only_when_loaded;
	HTTP_CUSTOM_PAGE_HANDLER page_generator;
};

bool check_custom_bound_path(const std::string& filename, struct custom_bound_entry* result);
int run_custom_page_generator(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id, struct custom_bound_entry* generator);
void add_custom_bound_path(HTTP_CUSTOM_PAGE_HANDLER page_generator,const char* path,const char* hostname = ANY_HOSTNAME_PATH,bool execute_only_when_loaded = true);
void load_custom_bound_paths();

#endif

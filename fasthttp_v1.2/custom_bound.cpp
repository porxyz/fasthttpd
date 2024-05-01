#include <string>
#include <unordered_map>
#include <cstdint>

#include "helper_functions.h"
#include "server_config.h"
#include "server_log.h"
#include "http_worker/http_worker.h"
#include "custom_bound.h"


std::unordered_map <std::string,struct custom_bound_entry> custom_bound_table;

bool check_custom_bound_path(const std::string& filename, struct custom_bound_entry* result)
{
	if(custom_bound_table.find(filename) != custom_bound_table.end())
	{
		result[0] = custom_bound_table[filename];
		return true;
	}

	return false;
}

int run_custom_page_generator(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id, struct custom_bound_entry* generator)
{
	struct HTTP2_STREAM *current_stream = NULL;

	//MOD_MYSQL auto reconnect
	#ifndef NO_MOD_MYSQL
	if(is_server_config_variable_true("enable_MOD_MYSQL") and is_server_config_variable_true("mysql_auto_reconnect"))
	{	
		http_workers[worker_id].mysql_db_handle->reconnect_if_gone();
	}
	#endif

	struct HTTP_CUSTOM_PAGE_HANDLER_ARGUMENTS handler_args;
	handler_args.worker_id = worker_id;
	handler_args.conn = conn;
	handler_args.stream_id = stream_id;
	
	if(conn->http_version == HTTP_VERSION_2)
	{
		struct HTTP2_CONNECTION *http2_conn = (struct HTTP2_CONNECTION *) conn->raw_connection;
		current_stream = &http2_conn->streams[stream_id];

		handler_args.request = &current_stream->request;
		handler_args.response = &current_stream->response;
	}
	else
	{
		struct HTTP1_CONNECTION *http1_conn = (struct HTTP1_CONNECTION*) conn->raw_connection;
		
		handler_args.request = &http1_conn->request;
		handler_args.response = &http1_conn->response;
	}

	if(generator->execute_only_when_loaded)
	{
		generator->page_generator(handler_args);

		handler_args.response->headers["content-length"] = int2str(handler_args.response->body.size());
		
		if(conn->http_version == HTTP_VERSION_2)
		{
			current_stream->state = HTTP_STATE_CONTENT_BOUND;
		}
		else
		{
			conn->state = HTTP_STATE_CONTENT_BOUND;
		}
		
		SERVER_LOG_REQUEST(conn, stream_id);

		return HTTP_Request_Send_Response(worker_id, conn, stream_id);
	}

	SERVER_LOG_REQUEST(conn, stream_id);
	return generator->page_generator(handler_args);
}

void add_custom_bound_path(HTTP_CUSTOM_PAGE_HANDLER page_generator, const char* path, const char* hostname, bool execute_only_when_loaded)
{
	struct custom_bound_entry new_entry;
	new_entry.page_generator = page_generator;
	new_entry.execute_only_when_loaded = execute_only_when_loaded;

	std::string custom_path;
	if(hostname == ANY_HOSTNAME_PATH)
	{
		for(auto i = SERVER_HOSTNAMES.begin(); i != SERVER_HOSTNAMES.end(); ++i)
		{
			custom_path = i->second;
			custom_path.append(1,'/');
			custom_path.append(path);
			custom_path = rectify_path(&custom_path);
			custom_bound_table[custom_path] = new_entry;
		}
	}

	else
	{
		if(SERVER_HOSTNAMES.find(hostname) == SERVER_HOSTNAMES.end())
		{
			return;
		}

		custom_path = SERVER_HOSTNAMES[hostname];
		custom_path.append(1,'/');
		custom_path.append(path);
		custom_path = rectify_path(&custom_path);
		custom_bound_table[custom_path] = new_entry;
	}

}


/*
 Here you can add custom handlers to different paths
 in order to suit your purpose
 */

//#include "custom_bound/index.h"
#include "custom_bound/post_test.h"
#include "custom_bound/cookie_test.h"

#include "custom_bound/bmp_grayscale.h"

#ifndef NO_MOD_MYSQL
#include "custom_bound/db_insert.h"
//#include "custom_bound/auth.h"
#endif

void load_custom_bound_paths()
{
	//add_custom_bound_path(index_gen,"/","localhost");
	add_custom_bound_path(post_test_gen, "/post_test", "localhost");
	add_custom_bound_path(cookie_test_gen,"/cookie_test","localhost");
	add_custom_bound_path(bmp_grayscale_generator,"/image.php");
	
	
	#ifndef NO_MOD_MYSQL
	//add_custom_bound_path(auth_generator,"/auth.php");
	add_custom_bound_path(db_insert_generator,"/db_insert","localhost");
	#endif
}


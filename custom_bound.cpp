#include <string>
#include <unordered_map>
#include <cstdint>

#include "helper_functions.h"
#include "server_config.h"
#include "http_worker.h"
#include "custom_bound.h"


std::unordered_map <std::string,struct custom_bound_entry> custom_bound_table;

bool check_custom_bound_path(const std::string& filename,struct custom_bound_entry* result)
{
	if(custom_bound_table.find(filename) != custom_bound_table.end())
	{
		result[0] = custom_bound_table[filename];
		return true;
	}

	return false;
}

void run_custom_page_generator(std::list<struct http_connection>::iterator* current_connection,size_t worker_id,struct custom_bound_entry* generator)
{
	if(generator->execute_only_when_loaded)
	{
		current_connection[0]->recv_buffer.clear();
		generator->page_generator(current_connection[0],worker_id);
		current_connection[0]->response.response_headers["Content-Length"] = int2str(current_connection[0]->response.response_body.size());
		generate_http_output(current_connection);

		if(!send_all(current_connection))
		{
			delete_http_connection(worker_id,current_connection);
			return;
		}

		if(current_connection[0]->send_buffer_offset == current_connection[0]->send_buffer.size())
			delete_http_connection(worker_id,current_connection,true);

		return; 
	}

	generator->page_generator(current_connection[0],worker_id);
}

void add_custom_bound_path(http_function_t page_generator,const char* path,const char* hostname,bool execute_only_when_loaded)
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
			return;

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

#ifndef NO_MOD_MYSQL
#include "custom_bound/db_insert.h"
#endif

void load_custom_bound_paths()
{
	//add_custom_bound_path(index_gen,"/","localhost");
	
	
	add_custom_bound_path(post_test_gen,"/post_test","localhost");
	add_custom_bound_path(cookie_test_gen,"/cookie_test","localhost");
	
	#ifndef NO_MOD_MYSQL
	add_custom_bound_path(db_insert_generator,"/db_insert","localhost");
	#endif
}

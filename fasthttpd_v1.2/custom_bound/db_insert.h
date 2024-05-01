#ifndef _page_db_insert_inc_
#define _page_db_insert_inc_

#include "../custom_bound.h"
#include "../mod_mysql.h"


int db_insert_generator(const struct HTTP_CUSTOM_PAGE_HANDLER_ARGUMENTS& args)
{
	#ifndef DISABLE_HTTPS
	if(!args.conn->https)
	{
		args.response->code = 400;
		echo("Only HTTPS requests possible!");
		return HTTP_CONNECTION_OK;
	}
	#endif

	std::vector<std::string> db_fields;
	std::vector<size_t> db_fields_sizes;
	
	db_fields.push_back("make");
	db_fields.push_back("model");
	db_fields.push_back("number");

	args.response->headers["content-type"]="application/json";
	
	for(unsigned int i=0; i<db_fields.size(); i++)
	{
		if(!HTTP_POST_ARG_EXISTS(db_fields[i]) or HTTP_POST_ARG(db_fields[i]).empty())
		{
			echo("{\"status\":\"error\"}");
			return HTTP_CONNECTION_OK;
		}
		
		db_fields_sizes.push_back(HTTP_POST_ARG(db_fields[i]).size());
	}
	
	mysql_stmt_query db_query(http_workers[args.worker_id].mysql_db_handle);

	if(!db_query.get_native_handle())
	{
		echo("{\"status\":\"db_error\"}");
		return HTTP_CONNECTION_OK;
	}

	db_query.prepare("INSERT INTO cars VALUES(DEFAULT, ?, ?, ?);", db_fields.size());
	if(db_query.get_last_errno() != 0)
	{
		echo("{\"status\":\"db_error\"}");
		return HTTP_CONNECTION_OK;
	}

	for(unsigned int i=0; i<db_fields.size(); i++)
	{
		db_query.bind_param(i, MYSQL_TYPE_STRING, (void*)HTTP_POST_ARG(db_fields[i]).c_str(), false, &db_fields_sizes[i]);
	}

	if(!db_query.execute())
	{
		echo("{\"status\":\"db_error\"}");
		return HTTP_CONNECTION_OK;
	}

	echo("{\"status\":\"success\",\"insert_id\":");
	echo( int2str(http_workers[args.worker_id].mysql_db_handle->last_insert_id()) );
	echo("}");

	return HTTP_CONNECTION_OK;
}


#endif


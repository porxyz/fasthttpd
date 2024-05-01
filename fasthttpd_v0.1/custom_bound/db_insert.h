#ifndef _page_db_insert_inc_
#define _page_db_insert_inc_

#include "../http_worker.h"
#include "../mod_mysql.h"


void db_insert_generator(std::list<struct http_connection>::iterator &http_connection,size_t worker_id)
{
	/*
	if(http_connection->https !== true)
	{
		http_connection->response.response_code = 400;
		echo("Only HTTPS requests possible!");
		return;
	}
	*/


	std::vector<std::string> db_fields;
	std::vector<size_t> db_fields_sizes;
	
	db_fields.push_back("marca");
	db_fields.push_back("model");
	db_fields.push_back("numar");

	http_connection->response.response_headers["Content-Type"]="application/json";


	
	for(unsigned int i=0; i<db_fields.size(); i++)
	{
		if(!HTTP_POST_ARG_EXISTS(db_fields[i]) or HTTP_POST_ARG(db_fields[i]).empty())
		{
			echo("{\"status\":\"error\"}");
			return;
		}
		
		db_fields_sizes.push_back(HTTP_POST_ARG(db_fields[i]).size());
	}
	

	mysql_stmt_query db_query(http_workers[worker_id].mysql_db_handle);

	if(!db_query.get_native_handle())
	{
		echo("{\"status\":\"db_error\"}");
		return;
	}


	db_query.prepare("INSERT INTO tanase VALUES(DEFAULT,?,?,?);",db_fields.size());
	if(db_query.get_last_errno() != 0)
	{
		echo("{\"status\":\"db_error\"}");
		return;
	}


	
	for(unsigned int i=0; i<db_fields.size(); i++)
	{
		db_query.bind_param(i,MYSQL_TYPE_STRING,(char*)HTTP_POST_ARG(db_fields[i]).c_str(),false,&db_fields_sizes[i]);
	}


	if(!db_query.execute())
	{
		echo("{\"status\":\"db_error\"}");
		return;
	}

	echo("{\"status\":\"success\",\"insert_id\":");
	echo( int2str(http_workers[worker_id].mysql_db_handle->last_insert_id()) );
	echo("}");
}


#endif


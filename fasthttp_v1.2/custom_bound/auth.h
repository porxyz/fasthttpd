#ifndef _page_auth_inc_
#define _page_auth_inc_


#include "../http_worker.h"
#include "../mod_mysql.h"

#include <openssl/rand.h>
#include <openssl/aes.h>

std::string compute_hashed_password(const std::string& password)
{
	uint8_t hash_buffer[SHA256_DIGEST_LENGTH];

	SHA256_CTX sha256_ctx;
	SHA256_Init(&sha256_ctx);
	SHA256_Update(&sha256_ctx,password.c_str(),password.size());
	SHA256_Final(hash_buffer,&sha256_ctx);

	std::string intermediate_value = password;
	intermediate_value.append((char*)hash_buffer,SHA256_DIGEST_LENGTH);
	intermediate_value.append(password);

	SHA256_Init(&sha256_ctx);
	SHA256_Update(&sha256_ctx,intermediate_value.c_str(),intermediate_value.size());
	SHA256_Final(hash_buffer,&sha256_ctx);

	return std::string((char*)hash_buffer,SHA256_DIGEST_LENGTH);
}




void auth_generator(std::list<struct http_connection>::iterator &http_connection,size_t worker_id)
{
	http_connection->response.response_headers["Content-Type"] = "application/json; charset=utf-8";

	if(!(HTTP_POST_ARG_EXISTS("username")) or !(HTTP_POST_ARG_EXISTS("password")))
	{
		echo("{\"status\":\"error\"}");
		return;
	}

	std::string hashed_password = compute_hashed_password(HTTP_POST_ARG("password"));

	mysql_stmt_query db_query(http_workers[worker_id].mysql_db_handle);
	if(!db_query.get_native_handle())
	{
		echo("{\"status\":\"db_error\"}");
		return;
	}

	db_query.prepare("SELECT ID FROM users WHERE username = ? and password = ?;",2,1);
	if(db_query.get_last_errno() != 0)
	{
		echo("{\"status\":\"db_error\"}");
		return;
	}

	size_t username_size = HTTP_POST_ARG("username").size();
	db_query.bind_param(0,MYSQL_TYPE_STRING,(char*)HTTP_POST_ARG("username").c_str(),false,&username_size);

	size_t hashed_password_size = hashed_password.size();
	db_query.bind_param(1,MYSQL_TYPE_BLOB,(uint8_t*)hashed_password.c_str(),false,&hashed_password_size);

	if(!db_query.execute())
	{
		echo("{\"status\":\"db_error\"}");
		return;
	}

	uint64_t user_ID;
	db_query.bind_result(0,MYSQL_TYPE_LONGLONG,&user_ID);

	db_query.store_result();

	if(db_query.num_rows() != 1)
	{
		echo("{\"status\":\"auth_error\"}");
		return;
	}

	db_query.fetch();


	uint8_t session_token[32];
	RAND_bytes(session_token,32);

	mysql_stmt_query insert_query(http_workers[worker_id].mysql_db_handle);
	if(!insert_query.get_native_handle())
	{
		echo("{\"status\":\"db_error\"}");
		return;
	}

	insert_query.prepare("INSERT INTO sessions VALUES(DEFAULT,?,?,?,UNIX_TIMESTAMP());",3);
	if(insert_query.get_last_errno() != 0)
	{
		echo("{\"status\":\"db_error\"}");
		return;
	}

	size_t session_token_size = 32;
	size_t remote_addr_buffer_size = http_connection->remote_addr.size();

	insert_query.bind_param(0,MYSQL_TYPE_LONGLONG,&user_ID);
	insert_query.bind_param(1,MYSQL_TYPE_BLOB,session_token,false,&session_token_size);
	insert_query.bind_param(2,MYSQL_TYPE_STRING,(char*)http_connection->remote_addr.c_str(),false,&remote_addr_buffer_size);

	if(!insert_query.execute())
	{
		echo("{\"status\":\"db_error\"}");
		return;
	}

	std::string session_ID = int2str(http_workers[worker_id].mysql_db_handle->last_insert_id());

	echo("{\"status\":\"success\",\"session_id\":");
	echo(session_ID);
	echo(",\"user_id\":");
	echo(int2str(user_ID));
	echo(",\"session_token\":\"");

	std::string hex_byte;
	for(size_t i=0; i<32; i++)
	{
		hex_byte = int2str(session_token[i],16);
		if(hex_byte.size() == 1){echo("0");}
		echo(hex_byte);
	}

	echo("\"}");
}


#endif

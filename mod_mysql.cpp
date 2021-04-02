#include <mutex>
#include <cstring>
#include <mysql/mysql.h>
#include <mysql/errmsg.h>

#include "helper_functions.h"
#include "server_journal.h"
#include "server_config.h"
#include "mod_mysql.h"

mysql_connection::mysql_connection() // default constructor
{
	this->mysql_handle = mysql_init(NULL);
	this->enable_error_journal = true;

	if(this->mysql_handle == NULL)
	{
		SERVER_JOURNAL_WRITE_ERROR.lock();
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
		SERVER_JOURNAL_WRITE(" MOD MYSQL: Unable to init mysql handle!\n",true);
		SERVER_JOURNAL_WRITE(this->get_last_error(),true);
		SERVER_JOURNAL_WRITE("\n\n",true);
		SERVER_JOURNAL_WRITE_ERROR.unlock();
	}

}


unsigned int mysql_connection::get_last_errno()
{
	return mysql_errno(this->mysql_handle);
}


const char* mysql_connection::get_last_error()
{
	return mysql_error(this->mysql_handle);
}

bool mysql_connection::set_database(const char* database_name)
{
	if(mysql_select_db(this->mysql_handle,database_name) != 0)
	{

		if(this->enable_error_journal)
		{
			SERVER_JOURNAL_WRITE_ERROR.lock();
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
			SERVER_JOURNAL_WRITE(" MOD MYSQL: Unable to set database!\n",true);
			SERVER_JOURNAL_WRITE(this->get_last_error(),true);
			SERVER_JOURNAL_WRITE("\n\n",true);
			SERVER_JOURNAL_WRITE_ERROR.unlock();
		}

		return false;
	}

	return true;
}


bool mysql_connection::connect(const char* hostname,const char* username,const char* password,const char* database_name,uint16_t port,const char* unix_socket)
{
	if(mysql_real_connect(this->mysql_handle,hostname,username,password,database_name,port,unix_socket,CLIENT_IGNORE_SIGPIPE) == NULL)
	{

		if(this->enable_error_journal)
		{
			SERVER_JOURNAL_WRITE_ERROR.lock();
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
			SERVER_JOURNAL_WRITE(" MOD MYSQL: Unable to connect!\n",true);
			SERVER_JOURNAL_WRITE(this->get_last_error(),true);
			SERVER_JOURNAL_WRITE("\n\n",true);
			SERVER_JOURNAL_WRITE_ERROR.unlock();
		}

		return false;
	}

	return true;
}

bool mysql_connection::set_option(enum mysql_option option, const void* opt_data)
{
	if(mysql_options(this->mysql_handle,option,opt_data) != 0)
	{

		if(this->enable_error_journal)
		{
			SERVER_JOURNAL_WRITE_ERROR.lock();
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
			SERVER_JOURNAL_WRITE(" MOD MYSQL: Unable to set option!\n",true);
			SERVER_JOURNAL_WRITE(this->get_last_error(),true); SERVER_JOURNAL_WRITE("\n\n",true);
			SERVER_JOURNAL_WRITE_ERROR.unlock();
		}

		return false;
	}

	return true;
}


bool mysql_connection::get_option(enum mysql_option option,void* opt_data)
{
	if(mysql_get_option(this->mysql_handle,option,opt_data) != 0)
	{

		if(this->enable_error_journal)
		{
			SERVER_JOURNAL_WRITE_ERROR.lock();
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
			SERVER_JOURNAL_WRITE(" MOD MYSQL: Unable to get option!\n",true);
			SERVER_JOURNAL_WRITE(this->get_last_error(),true);
			SERVER_JOURNAL_WRITE("\n\n",true);
			SERVER_JOURNAL_WRITE_ERROR.unlock();
		}

		return false;
	}

	return true;
}


bool mysql_connection::is_alive()
{
	int mysql_ping_status = mysql_ping(this->mysql_handle);

	if(mysql_ping_status == 0)
		return true;

	if(this->get_last_errno() == CR_SERVER_GONE_ERROR or this->get_last_errno() == CR_SERVER_LOST)
		return false;

	if(this->enable_error_journal)
	{
		SERVER_JOURNAL_WRITE_ERROR.lock();
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
		SERVER_JOURNAL_WRITE(" MOD MYSQL: Unable to check if the connection is alive!\n",true);
		SERVER_JOURNAL_WRITE(this->get_last_error(),true);
		SERVER_JOURNAL_WRITE("\n\n",true);
		SERVER_JOURNAL_WRITE_ERROR.unlock();
	}


	return false;
}



bool mysql_connection::reset(bool hard_reset)
{
	if(hard_reset)
	{
		mysql_close(this->mysql_handle);

		this->mysql_handle = mysql_init(NULL);

		if(this->mysql_handle == NULL)
		{
			SERVER_JOURNAL_WRITE_ERROR.lock();
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
			SERVER_JOURNAL_WRITE(" MOD MYSQL: Unable to init mysql handle!\n",true);
			SERVER_JOURNAL_WRITE(this->get_last_error(),true);
			SERVER_JOURNAL_WRITE("\n\n",true);
			SERVER_JOURNAL_WRITE_ERROR.unlock();
			return false;
		}

		init_MYSQL_connection(this);
		
		if(this->get_last_errno() != 0)
			return false;
	}


	else
	{

		if(mysql_reset_connection(this->mysql_handle) != 0)
		{

			if(this->enable_error_journal)
			{
				SERVER_JOURNAL_WRITE_ERROR.lock();
				SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
				SERVER_JOURNAL_WRITE(" MOD MYSQL: Unable to reset the connection!\n",true);
				SERVER_JOURNAL_WRITE(this->get_last_error(),true);
				SERVER_JOURNAL_WRITE("\n\n",true);
				SERVER_JOURNAL_WRITE_ERROR.unlock();
			}

			return false;
		}
	}

	return true;
}


uint64_t mysql_connection::last_insert_id()
{
	return mysql_insert_id(this->mysql_handle);
}

bool mysql_stmt_query::prepare(const std::string& sql,unsigned int bind_params_num,unsigned int bind_result_num)
{

	if(mysql_stmt_prepare(this->query_handle,sql.c_str(),sql.size()) != 0)
	{

		if(this->enable_error_journal)
		{
			SERVER_JOURNAL_WRITE_ERROR.lock();
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
			SERVER_JOURNAL_WRITE(" MOD MYSQL: Unable to prepare mysql stmt!\n",true);
			SERVER_JOURNAL_WRITE(this->get_last_error(),true);
			SERVER_JOURNAL_WRITE("\n\n",true);
			SERVER_JOURNAL_WRITE_ERROR.unlock();
		}

		return false;
	}

	if((bind_params_num + bind_result_num) != 0)
	{
		this->query_params = new (std::nothrow) MYSQL_BIND[bind_params_num + bind_result_num];

		if(this->query_params == NULL)
		{
			if(this->enable_error_journal)
				SERVER_ERROR_JOURNAL_stdlib_err("MOD MYSQL: Unable to allocate memory for mysql bind structs!");

			return false;
		}

		this->query_params_num = bind_params_num;
		this->result_params_num = bind_result_num;

		memset(this->query_params,0,sizeof(MYSQL_BIND) * (bind_params_num + bind_result_num));
	}


	return true;
}

void mysql_connection::close()
{
	if(this->mysql_handle != NULL)
	{
		mysql_close(this->mysql_handle);
		this->mysql_handle = NULL;
	}
}

mysql_connection::~mysql_connection() // destructor
{
	this->close();
}



mysql_stmt_query::mysql_stmt_query(const void* mysql_conn)
{
	mysql_connection* conn = (mysql_connection*)mysql_conn;
	this->query_handle = mysql_stmt_init(conn->mysql_handle);
	this->enable_error_journal = conn->enable_error_journal;
	this->query_params = NULL;
	this->query_params_num = 0;
	this->result_params_num = 0;
	this->result_params_bounded = false;

	if(this->query_handle == NULL)
	{

		if(this->enable_error_journal)
		{
			SERVER_JOURNAL_WRITE_ERROR.lock();
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
			SERVER_JOURNAL_WRITE(" MOD MYSQL: Unable to init mysql stmt!\n",true);
			SERVER_JOURNAL_WRITE(this->get_last_error(),true);
			SERVER_JOURNAL_WRITE("\n\n",true);
			SERVER_JOURNAL_WRITE_ERROR.unlock();
		}

	}

}



void mysql_stmt_query::bind_param(unsigned int param_number,enum enum_field_types data_type,void* data,bool is_unsigned,unsigned long* len)
{
	if(param_number >= this->query_params_num)
	{

		if(this->enable_error_journal)
		{
			SERVER_JOURNAL_WRITE_ERROR.lock();
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
			SERVER_JOURNAL_WRITE(" MOD MYSQL: Unable to bind param to mysql stmt!\n",true);
			SERVER_JOURNAL_WRITE("Param number out of range!\n\n",true);
			SERVER_JOURNAL_WRITE_ERROR.unlock();
		}


	}	

	this->query_params[param_number].buffer_type = data_type;
	this->query_params[param_number].buffer = data;
	this->query_params[param_number].is_unsigned = is_unsigned;
	this->query_params[param_number].length = len;
}


bool mysql_stmt_query::execute()
{
	if(this->query_params != NULL)
	{

		int r = mysql_stmt_bind_param(this->query_handle,this->query_params);

		if(r != 0)
		{

			if(this->enable_error_journal)
			{
				SERVER_JOURNAL_WRITE_ERROR.lock();
				SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
				SERVER_JOURNAL_WRITE(" MOD MYSQL: Unable to bind mysql stmt!\n",true);
				SERVER_JOURNAL_WRITE(this->get_last_error(),true);
				SERVER_JOURNAL_WRITE("\n\n",true);
				SERVER_JOURNAL_WRITE_ERROR.unlock();
			}

			return false;
		}


	}

	if(mysql_stmt_execute(this->query_handle) != 0)
	{

		if(this->enable_error_journal)
		{
			SERVER_JOURNAL_WRITE_ERROR.lock();
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
			SERVER_JOURNAL_WRITE(" MOD MYSQL: Unable to execute mysql stmt!\n",true);
			SERVER_JOURNAL_WRITE(this->get_last_error(),true);
			SERVER_JOURNAL_WRITE("\n\n",true);
			SERVER_JOURNAL_WRITE_ERROR.unlock();
		}

		return false;
	}


	return true;
}


bool mysql_stmt_query::store_result()
{
	if(!this->result_params_bounded)
	{

		if(mysql_stmt_bind_result(this->query_handle,this->query_params + this->query_params_num) != 0)
		{

			if(this->enable_error_journal)
			{
				SERVER_JOURNAL_WRITE_ERROR.lock();
				SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
				SERVER_JOURNAL_WRITE(" MOD MYSQL: Unable to bind mysql stmt result!\n",true);
				SERVER_JOURNAL_WRITE(this->get_last_error(),true);
				SERVER_JOURNAL_WRITE("\n\n",true);
				SERVER_JOURNAL_WRITE_ERROR.unlock();
			}

			return false;
		}

		this->result_params_bounded = true;
	}

	if(mysql_stmt_store_result(this->query_handle) != 0)
	{

		if(this->enable_error_journal)
		{
			SERVER_JOURNAL_WRITE_ERROR.lock();
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
			SERVER_JOURNAL_WRITE(" MOD MYSQL: Unable to store mysql stmt result!\n",true);
			SERVER_JOURNAL_WRITE(this->get_last_error(),true);
			SERVER_JOURNAL_WRITE("\n\n",true);
			SERVER_JOURNAL_WRITE_ERROR.unlock();
		}

		return false;
	}


	return true;
}


bool mysql_stmt_query::fetch()
{
	if(!this->result_params_bounded)
	{

		if(mysql_stmt_bind_result(this->query_handle,this->query_params + this->query_params_num) != 0)
		{

			if(this->enable_error_journal)
			{
				SERVER_JOURNAL_WRITE_ERROR.lock();
				SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
				SERVER_JOURNAL_WRITE(" MOD MYSQL: Unable to bind mysql stmt result!\n",true);
				SERVER_JOURNAL_WRITE(this->get_last_error(),true);
				SERVER_JOURNAL_WRITE("\n\n",true);
				SERVER_JOURNAL_WRITE_ERROR.unlock();
			}

			return false;
		}

		this->result_params_bounded = true;
	}

	int r = mysql_stmt_fetch(this->query_handle);

	if(r == 1)
	{

		if(this->enable_error_journal)
		{
			SERVER_JOURNAL_WRITE_ERROR.lock();
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
			SERVER_JOURNAL_WRITE(" MOD MYSQL: Unable to fetch mysql stmt result!\n",true);
			SERVER_JOURNAL_WRITE(this->get_last_error(),true);
			SERVER_JOURNAL_WRITE("\n\n",true);
			SERVER_JOURNAL_WRITE_ERROR.unlock();
		}

		return false;
	}

	if(r != 0)
		return false;


	return true;
}

void mysql_stmt_query::bind_result(unsigned int param_number,enum enum_field_types data_type,void* data,bool* is_null,unsigned long* len,unsigned long buffer_size,bool* truncated)
{

	if(param_number >= this->result_params_num)
	{

		if(this->enable_error_journal)
		{
			SERVER_JOURNAL_WRITE_ERROR.lock();
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
			SERVER_JOURNAL_WRITE(" MOD MYSQL: Unable to bind param to mysql stmt!\n",true);
			SERVER_JOURNAL_WRITE("Param number out of range!\n\n",true);
			SERVER_JOURNAL_WRITE_ERROR.unlock();
		}


	}		


	this->query_params[param_number + this->query_params_num].buffer_type = data_type;
	this->query_params[param_number + this->query_params_num].buffer = data;
	this->query_params[param_number + this->query_params_num].is_null = (bool*)is_null;
	this->query_params[param_number + this->query_params_num].length = len;
	this->query_params[param_number + this->query_params_num].buffer_length = buffer_size;
	this->query_params[param_number + this->query_params_num].error =  (bool*)truncated;
	
}

size_t mysql_stmt_query::num_rows()
{
	return mysql_stmt_num_rows(this->query_handle);
}

unsigned int mysql_stmt_query::get_last_errno()
{
	return mysql_stmt_errno(this->query_handle);
}

const char* mysql_stmt_query::get_last_error()
{
	return mysql_stmt_error(this->query_handle);
}


void mysql_stmt_query::close()
{
	if(this->query_handle != NULL)
	{
		mysql_stmt_close(this->query_handle);
		this->query_handle = NULL;
	}
	
	if(this->query_params != NULL)
	{
		delete(this->query_params);
		this->query_params = NULL;
	}
	
	this->query_params_num = 0;
	this->result_params_num = 0;
	this->result_params_bounded = false;
}

mysql_stmt_query::~mysql_stmt_query()
{
	this->close();
}



void init_MYSQL_connection(mysql_connection* mysql_handle)
{

	if(is_server_config_variable_false("mysql_error_logging"))
		mysql_handle->enable_error_journal = false;
		

	if(server_config_variable_exists("mysql_connection_timeout"))
	{
		unsigned int db_conn_timeout = str2uint(&SERVER_CONFIGURATION["mysql_connection_timeout"]);
		mysql_handle->set_option(MYSQL_OPT_CONNECT_TIMEOUT,&db_conn_timeout);
	}

	if(is_server_config_variable_true("mysql_auto_reconnect"))
	{	
		bool mysql_auto_reconn = true;
		mysql_handle->set_option(MYSQL_OPT_RECONNECT,&mysql_auto_reconn);
	}

	if(is_server_config_variable_false("mysql_auto_reconnect"))
	{	
		bool mysql_auto_reconn = false;
		mysql_handle->set_option(MYSQL_OPT_RECONNECT,&mysql_auto_reconn);
	}

	
	mysql_handle->connect(SERVER_CONFIGURATION["mysql_hostname"].c_str(),SERVER_CONFIGURATION["mysql_username"].c_str(),
	SERVER_CONFIGURATION["mysql_password"].c_str(),(server_config_variable_exists("mysql_database") ? SERVER_CONFIGURATION["mysql_database"].c_str() : NULL) ,
	(server_config_variable_exists("mysql_port") ? str2uint(&SERVER_CONFIGURATION["mysql_database"]) : 0) ,
	(server_config_variable_exists("mysql_unix_socket") ? SERVER_CONFIGURATION["mysql_unix_socket"].c_str() : NULL));

}



#ifndef __mod_mysql_incl__
#define __mod_mysql_incl__

#include <string>
#include <mysql/mysql.h>

class mysql_connection
{
public:
	mysql_connection();

	bool connect(const char* hostname,const char* username,const char* password = NULL,const char* database_name = NULL,uint16_t port = 0,
	const char* unix_socket = NULL);

	bool set_database(const char* database_name);
	bool get_database(std::string* output);

	bool set_option(enum mysql_option option, const void* opt_data);
	bool get_option(enum mysql_option option,void* opt_data);
	bool reset(bool hard_reset = true);
	bool is_alive();
	bool reconnect_if_gone();
	uint64_t last_insert_id();
	unsigned int get_last_errno();
	const char* get_last_error();
	bool error_log_enabled();
	void enable_error_logging(bool enable);
	MYSQL* get_native_handle();
	void close();
	~mysql_connection();

protected:
	MYSQL* mysql_handle;
	bool enable_error_log;
};


class mysql_stmt_query
{
public:
	mysql_stmt_query() = delete;
	mysql_stmt_query(mysql_connection* mysql_conn);
	bool bind_param(unsigned int param_number,enum enum_field_types data_type,void* data,bool is_unsigned = false,unsigned long* len = NULL);

	bool bind_result(unsigned int result_number,enum enum_field_types data_type,void* data,bool* is_null = NULL,unsigned long* len = NULL,
	unsigned long buffer_size = 0,bool* err = NULL);

	bool execute();
	bool store_result();
	bool fetch();
	size_t num_rows();

	unsigned int get_last_errno();
	const char* get_last_error();
	MYSQL_STMT* get_native_handle();
	bool error_log_enabled();
	void enable_error_logging(bool enable);


	bool prepare(const std::string& sql,unsigned int bind_params_num = 0,unsigned int bind_result_num = 0);
	void close();
	~mysql_stmt_query();

protected:
	MYSQL_STMT* query_handle;
	bool enable_error_log;
	MYSQL_BIND* query_params;
	unsigned int query_params_num;
	unsigned int result_params_num;
	bool result_params_bounded;
};


void init_MYSQL_connection(mysql_connection*);

#endif

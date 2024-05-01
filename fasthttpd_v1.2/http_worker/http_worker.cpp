#include "http_worker.h"
#include "hpack_api.h"

#include "../server_config.h"
#include "../server_log.h"
#include "../file_permissions.h"
#include "../helper_functions.h"

#include <unistd.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>

#include <cstring>

std::vector<struct HTTP_WORKER_NODE> http_workers;
std::atomic<size_t> total_http_connections;

// for round robin load balancing
unsigned int next_worker = 0;
std::mutex next_worker_mutex;

// for fair load balancing
bool is_server_load_balancer_fair;
std::vector<std::atomic<int>> connections_per_worker;


int Network_Read_Bytes(struct GENERIC_HTTP_CONNECTION *conn, void *buffer, size_t len)
{
	int result;

	while (true)
	{
		if (!conn->https)
		{
			result = recv(conn->client_sock, buffer, len, 0);

			if (result < 0)
			{
				if (errno == EINTR)
				{
					continue;
				}

				if (errno == EAGAIN or errno == EWOULDBLOCK)
				{
					return 0;
				}

				SERVER_ERROR_LOG_stdlib_err("Unable to read from client socket!");

				return -1;
			}

			break;
		}
		#ifndef DISABLE_HTTPS
		else
		{
			result = SSL_read(conn->ssl_wrapper, buffer, len);

			if (result < 0)
			{
				int error_code = SSL_get_error(conn->ssl_wrapper, result);
				if (error_code == SSL_ERROR_WANT_READ or error_code == SSL_ERROR_WANT_WRITE)
				{
					return 0;
				}

				SERVER_ERROR_LOG_openssl_err("Unable to read from SSL!");

				return -1;
			}

			break;
		}
		#endif
	}

	return result;
}

int Network_Write_Bytes(struct GENERIC_HTTP_CONNECTION *conn, void *buffer, size_t len)
{
	int result;

	while (true)
	{
		if (!conn->https)
		{
			result = send(conn->client_sock, buffer, len, 0);

			if (result < 0)
			{
				if (errno == EINTR)
				{
					continue;
				}

				if (errno == EAGAIN or errno == EWOULDBLOCK)
				{
					return 0;
				}

				SERVER_ERROR_LOG_stdlib_err("Unable to write to client socket!");

				return -1;
			}

			break;
		}
		#ifndef DISABLE_HTTPS
		else
		{
			result = SSL_write(conn->ssl_wrapper, buffer, len);

			if (result < 0)
			{
				int error_code = SSL_get_error(conn->ssl_wrapper, result);
				if (error_code == SSL_ERROR_WANT_READ or error_code == SSL_ERROR_WANT_WRITE)
				{
					return 0;
				}

				SERVER_ERROR_LOG_openssl_err("Unable to write to SSL!");

				return -1;
			}

			break;
		}
		#endif
	}

	return result;
}

void HTTP_Worker_Add_Client(HTTP_Worker_Add_Client_Parameters& params)
{
	int worker_id;

	if (is_server_load_balancer_fair)
	{
		worker_id = 0;
		int min_connections = connections_per_worker[0];

		size_t n_workers = connections_per_worker.size();
		for (size_t i = 1; i < n_workers; i++)
		{
			if (connections_per_worker[i] < min_connections)
			{
				min_connections = connections_per_worker[i];
				worker_id = i;
			}
		}
	}
	else
	{
		next_worker_mutex.lock();

		worker_id = next_worker;
		next_worker++;

		if (next_worker >= http_workers.size())
		{
			next_worker = 0;
		}

		next_worker_mutex.unlock();
	}

	struct GENERIC_HTTP_CONNECTION current_connection;

	current_connection.client_sock = params.client_sock;
	current_connection.remote_addr = params.remote_addr;
	current_connection.server_addr = params.server_addr;
	current_connection.remote_port = params.remote_port;
	current_connection.server_port = params.server_port;
	current_connection.ip_addr_version = params.ip_addr_version;
	current_connection.milisecond_timeout = str2uint(&SERVER_CONFIGURATION["request_timeout"]) * 1000;
	current_connection.https = params.https;

	// init a http/1.1 connection
	if (!params.https)
	{
		current_connection.http_version = HTTP_VERSION_1_1;
		current_connection.state = HTTP_STATE_WAIT_PATH;

		struct HTTP1_CONNECTION *http_conn = new (std::nothrow) struct HTTP1_CONNECTION();
		if(!http_conn)
		{
			SERVER_ERROR_LOG_stdlib_err("Unable to allocate memory for a HTTP1_CONNECTION.");
			exit(-1);
		}

		current_connection.raw_connection = http_conn;

		HTTP1_Connection_Init(http_conn);
	}

#ifndef DISABLE_HTTPS
	else
	{
		current_connection.http_version = HTTP_VERSION_UNDEFINED;
		current_connection.state = HTTP_STATE_SSL_INIT;

		current_connection.ssl_wrapper = SSL_new(params.openssl_ctx);
		if (current_connection.ssl_wrapper == NULL)
		{
			SERVER_ERROR_LOG_openssl_err("Unable to allocate a SSL!");
			exit(-1);
		}

		SSL_set_fd(current_connection.ssl_wrapper, params.client_sock);
	}
#endif

	if (clock_gettime(CLOCK_MONOTONIC, &current_connection.last_action) == -1)
	{
		SERVER_ERROR_LOG_stdlib_err("Unable to get time!");
		exit(-1);
	}

	http_workers[worker_id].connections_mutex->lock();
	http_workers[worker_id].connections[params.client_sock] = current_connection;

	struct epoll_event epoll_config;
	memset(&epoll_config, 0, sizeof(epoll_config)); //suppress valgrind warnings
	epoll_config.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
	epoll_config.data.fd = params.client_sock;

	if (epoll_ctl(http_workers[worker_id].worker_epoll, EPOLL_CTL_ADD, params.client_sock, &epoll_config) == -1)
	{
		SERVER_ERROR_LOG_stdlib_err("Unable to add the client to the worker epoll!");
		exit(-1);
	}

	if (is_server_load_balancer_fair)
	{
		connections_per_worker[worker_id]++;
	}

	http_workers[worker_id].connections_mutex->unlock();
}

void close_all_expired_connections(const int worker_id, const struct timespec& current_time)
{	
	http_workers[worker_id].connections_mutex->lock();

	for (auto i = http_workers[worker_id].connections.begin(); i != http_workers[worker_id].connections.end(); ++i)
	{
		int64_t elapsed_miliseconds = ((current_time.tv_sec * 1000) + (current_time.tv_nsec / 1000000)) - 
		((i->second.last_action.tv_sec * 1000) + (i->second.last_action.tv_nsec / 1000000));

		// infinite timeout
		if (i->second.milisecond_timeout == 0)
		{
			continue;
		}

		if (elapsed_miliseconds > i->second.milisecond_timeout)
		{	
			auto expired_connection = i;
			i++;

			Generic_Connection_Delete(worker_id, &expired_connection->second, false);
			
			if(i == http_workers[worker_id].connections.end())
			{
				break;
			}
		}
	}

	http_workers[worker_id].connections_mutex->unlock();
}

#ifndef DISABLE_HTTPS
void Accept_SSL_Connection(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn)
{
	int ssl_status = SSL_accept(conn->ssl_wrapper);
	if (ssl_status <= 0)
	{
		int ssl_errno = SSL_get_error(conn->ssl_wrapper, ssl_status);

		if (ssl_errno == SSL_ERROR_WANT_READ or ssl_errno == SSL_ERROR_WANT_WRITE)
		{
			return;
		}

		SERVER_ERROR_LOG_openssl_err("Unable to accept the incoming SSL connection!");

		Generic_Connection_Delete(worker_id, conn);
		return;
	}

	const uint8_t *ALPN_selected_ext = NULL;
	unsigned int ALPN_selected_ext_len = 0;
	SSL_get0_alpn_selected(conn->ssl_wrapper, &ALPN_selected_ext, &ALPN_selected_ext_len);

	if (ALPN_selected_ext_len == 2 and memcmp("h2", ALPN_selected_ext, 2) == 0) // ALPN h2
	{
		conn->http_version = HTTP_VERSION_2;
	}
	else if (ALPN_selected_ext_len == 8 and memcmp("http/1.1", ALPN_selected_ext, 8) == 0) // ALPN http/1.1
	{
		conn->http_version = HTTP_VERSION_1_1;
	}
	else // no APLN negotiation
	{
		conn->http_version = HTTP_VERSION_1_1;
	}

	if (conn->http_version == HTTP_VERSION_2)
	{
		conn->state = HTTP2_CONNECTION_STATE_WAIT_HELLO;

		struct HTTP2_CONNECTION *http2_conn = new (std::nothrow) struct HTTP2_CONNECTION();
		if(!http2_conn)
		{
			SERVER_ERROR_LOG_stdlib_err("Unable to allocate memory for a HTTP2_CONNECTION.");
			exit(-1);
		}

		conn->raw_connection = http2_conn;

		HTTP2_Connection_Init(http2_conn);
		HTTP2_Connection_Process(worker_id, conn);
	}
	else
	{
		conn->state = HTTP_STATE_WAIT_PATH;

		struct HTTP1_CONNECTION *http_conn = new (std::nothrow) struct HTTP1_CONNECTION();
		if(!http_conn)
		{
			SERVER_ERROR_LOG_stdlib_err("Unable to allocate memory for a HTTP1_CONNECTION.");
			exit(-1);
		}
				
		conn->raw_connection = http_conn;

		HTTP1_Connection_Init(http_conn);
		HTTP1_Connection_Process(worker_id, conn);
	}
}
#endif

void Generic_Connection_Delete(const int worker_id, struct GENERIC_HTTP_CONNECTION* conn, bool lock_mutex)
{
	if(conn->http_version == HTTP_VERSION_2)
	{
		HTTP2_Connection_Delete(worker_id, (struct HTTP2_CONNECTION*)conn->raw_connection);
		delete( ((struct HTTP2_CONNECTION*)conn->raw_connection) );
	}
	else if(conn->http_version == HTTP_VERSION_1 or conn->http_version == HTTP_VERSION_1_1)
	{
		HTTP1_Connection_Delete((struct HTTP1_CONNECTION*)conn->raw_connection);
		delete( ((struct HTTP1_CONNECTION*)conn->raw_connection) );
	}

	#ifndef DISABLE_HTTPS
	if(conn->https)
	{
		SSL_free(conn->ssl_wrapper);
	}
	#endif

	close(conn->client_sock);

	if(lock_mutex)
	{
		http_workers[worker_id].connections_mutex->lock();
		http_workers[worker_id].connections.erase(conn->client_sock);
		http_workers[worker_id].connections_mutex->unlock();
	}
	else
	{
		http_workers[worker_id].connections.erase(conn->client_sock);
	}

	total_http_connections--;

	if(is_server_load_balancer_fair)
	{
		connections_per_worker[worker_id]--;
	}
}

void http_worker_thread(int worker_id)
{
	HTTP_Worker_Init_Aux_Modules(worker_id);

	struct epoll_event triggered_events[128];

	size_t recv_buffer_size = str2uint(SERVER_CONFIGURATION["read_buffer_size"]) * 1024;
	http_workers[worker_id].recv_buffer = new (std::nothrow) char[recv_buffer_size];
	if (!http_workers[worker_id].recv_buffer)
	{
		SERVER_ERROR_LOG_stdlib_err("Unable to allocate memory for the http worker recv buffer!");
		exit(-1);
	}

	int epoll_wait_time = 500;
	bool epoll_loop_should_stop = false;
	while (!epoll_loop_should_stop)
	{
		int epoll_result = epoll_wait(http_workers[worker_id].worker_epoll, triggered_events, sizeof(triggered_events) / sizeof(struct epoll_event), epoll_wait_time);

		if (epoll_result == -1)
		{
			if (errno != EINTR)
			{
				SERVER_ERROR_LOG_stdlib_err("Unable to listen to epoll!");
				exit(-1);
			}

			continue;
		}

		struct timespec current_time;
		if (clock_gettime(CLOCK_MONOTONIC, &current_time) == -1)
		{
			SERVER_ERROR_LOG_stdlib_err("Unable to get time!");
			exit(-1);
		}

		for (int event_num = 0; event_num < epoll_result; event_num++)
		{
			struct epoll_event triggered_event = triggered_events[event_num];

			// shutdown triggered
			if (triggered_event.data.fd == 0)
			{
				epoll_loop_should_stop = true;
				break;
			}

			struct GENERIC_HTTP_CONNECTION *triggered_connection = NULL;
			auto triggered_connection_it = http_workers[worker_id].connections.find(triggered_event.data.fd);

			if(triggered_connection_it != http_workers[worker_id].connections.end())
			{
				triggered_connection = &triggered_connection_it->second;
			}
			else
			{
				//the connection was deleted
				continue;
			}
			
			triggered_connection->last_action = current_time;

			if (triggered_event.events & EPOLLHUP or triggered_event.events & EPOLLRDHUP or triggered_event.events & EPOLLERR)
			{
				Generic_Connection_Delete(worker_id, triggered_connection);
				continue;
			}

			if (triggered_connection->state == HTTP_STATE_SSL_INIT and triggered_connection->http_version == HTTP_VERSION_UNDEFINED)
			{
				#ifndef DISABLE_HTTPS
				Accept_SSL_Connection(worker_id, triggered_connection);
				#endif
			}
			else
			{
				// process the connection
				if (triggered_connection->http_version == HTTP_VERSION_2)
				{
					HTTP2_Connection_Process(worker_id, triggered_connection);
				}
				else if (triggered_connection->http_version == HTTP_VERSION_1 or triggered_connection->http_version == HTTP_VERSION_1_1)
				{
					HTTP1_Connection_Process(worker_id, triggered_connection);
				}
			}
		}

		close_all_expired_connections(worker_id, current_time);
	}

	delete[] http_workers[worker_id].recv_buffer;

	//free all connections
	http_workers[worker_id].connections_mutex->lock();
	for(auto it = http_workers[worker_id].connections.begin(); it != http_workers[worker_id].connections.end(); it++)
	{
		auto temp_it = it;
		it++;

		Generic_Connection_Delete(worker_id, &temp_it->second, false);

		if(it == http_workers[worker_id].connections.end())
		{
			break;
		}
	}
	http_workers[worker_id].connections_mutex->unlock();

	//delete the connections mutex
	delete(http_workers[worker_id].connections_mutex);

	//close the epoll fd
	close(http_workers[worker_id].worker_epoll);

	HTTP_Worker_Free_Aux_Modules(worker_id);
}

void HTTP_Workers_Init(int close_trigger)
{
	size_t num_workers = str2uint(&SERVER_CONFIGURATION["server_workers"]);
	init_file_access_control_API(num_workers);

	if (is_server_load_balancer_fair)
	{
		connections_per_worker = std::vector<std::atomic<int>>(num_workers);
	}

	for (unsigned int i = 0; i < num_workers; i++)
	{
		struct HTTP_WORKER_NODE this_worker;

		this_worker.worker_epoll = epoll_create1(EPOLL_CLOEXEC);
		if (this_worker.worker_epoll == -1)
		{
			SERVER_ERROR_LOG_stdlib_err("Unable to create the http worker epoll!");
			exit(-1);
		}

		struct epoll_event epoll_config;
		epoll_config.events = EPOLLIN | EPOLLET;
		epoll_config.data.ptr = 0;

		if (epoll_ctl(this_worker.worker_epoll, EPOLL_CTL_ADD, close_trigger, &epoll_config) == -1)
		{
			SERVER_ERROR_LOG_stdlib_err("Unable to add the server close trigger to epoll!");
			exit(-1);
		}

		http_workers.push_back(this_worker);
		/*
		create the mutex first, 
		to avoid locking a non existent mutex
		*/
		http_workers[http_workers.size() - 1].connections_mutex = new std::mutex();
		http_workers[http_workers.size() - 1].worker_thread = new std::thread(http_worker_thread, http_workers.size() - 1);
	}

	SERVER_LOG_WRITE_NORMAL.lock();
	SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING));
	SERVER_LOG_WRITE(" The server started successfully!\n");
	SERVER_LOG_WRITE_NORMAL.unlock();
}

void HTTP_Workers_Join()
{
	for (unsigned int i = 0; i < http_workers.size(); i++)
	{
		http_workers[i].worker_thread->join();
		delete (http_workers[i].worker_thread);
	}
}

void HTTP_Worker_Init_Aux_Modules(int worker_id)
{
#ifndef NO_MOD_MYSQL
	http_workers[worker_id].mysql_db_handle = new (std::nothrow) mysql_connection();
	if (http_workers[worker_id].mysql_db_handle == NULL)
	{
		SERVER_ERROR_LOG_stdlib_err("MOD_MYSQL: Unable to allocate memory for the mysql connection!");
		exit(-1);
	}

	if (is_server_config_variable_true("enable_MOD_MYSQL"))
	{
		init_MYSQL_connection(http_workers[worker_id].mysql_db_handle);
	}
#endif
}

void HTTP_Worker_Free_Aux_Modules(int worker_id)
{
#ifndef NO_MOD_MYSQL
	delete (http_workers[worker_id].mysql_db_handle);
#endif
}
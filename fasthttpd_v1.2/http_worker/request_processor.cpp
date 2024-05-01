#include "request_processor.h"
#include "http_worker.h"
#include "http_parser.h"
#include "hpack_api.h"

#include "../server_config.h"
#include "../server_log.h"
#include "../custom_bound.h"
#include "../file_permissions.h"
#include "../helper_functions.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>

#include <cstring>
#include <cstdlib>

int HTTP_Request_Send_Response(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id)
{
	if(conn->http_version == HTTP_VERSION_2)
	{
		struct HTTP2_CONNECTION *http2_conn = (struct HTTP2_CONNECTION*) conn->raw_connection;;
		struct HTTP2_STREAM *current_stream = &http2_conn->streams[stream_id];

		HPACK_encode_headers(http2_conn->hpack_encoder, &current_stream->response, &current_stream->send_buffer);
		return HTTP2_Stream_Send_Headers(worker_id, conn, stream_id);
	}
	else
	{
		HTTP1_Connection_Generate_Response(conn);
		return HTTP1_Connection_Send_Data(worker_id, conn);
	}
}

int HTTP_Request_Set_Error_Page(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id, const int error_code, std::string reason)
{
	struct HTTP1_CONNECTION *http1_conn = NULL; 

	struct HTTP2_CONNECTION *http2_conn = NULL;
	struct HTTP2_STREAM *current_stream = NULL;

	struct HTTP_REQUEST *http_request = NULL;
	struct HTTP_RESPONSE *http_response = NULL;
	
	if(conn->http_version == HTTP_VERSION_2)
	{
		http2_conn = (struct HTTP2_CONNECTION*) conn->raw_connection;
		current_stream = &http2_conn->streams[stream_id];
		http_request = &current_stream->request;
		http_response = &current_stream->response;
	}
	else
	{
		http1_conn = (struct HTTP1_CONNECTION*) conn->raw_connection;
		http_request = &http1_conn->request;
		http_response = &http1_conn->response;
	}

	http_response->code = error_code;
	
	if (server_error_page_exists(error_code))
	{
		http_response->body = SERVER_ERROR_PAGES[error_code];
	}
	else
	{
		reason = "Error ";
		reason.append(int2str(error_code));
		reason.append(" encountered but no error page found!");

		http_response->code = 500;
		http_response->body = SERVER_ERROR_PAGES[500];
	}

	str_replace_first(&http_response->body, "$SERVER_NAME", &SERVER_CONFIGURATION["server_name"]);
	str_replace_first(&http_response->body, "$SERVER_VERSION", &SERVER_CONFIGURATION["server_version"]);
	str_replace_first(&http_response->body, "$OS_NAME", &SERVER_CONFIGURATION["os_name"]);
	str_replace_first(&http_response->body, "$OS_VERSION", &SERVER_CONFIGURATION["os_version"]);
	str_replace_first(&http_response->body, "$SERVER_PORT", int2str(conn->server_port).c_str());
	str_replace_first(&http_response->body, "$REASON", &reason);

	std::string path_url;
	if (http_request->URI_path.size() > 128)
	{
		path_url = http_request->URI_path.substr(0, 128);
		path_url.append("...");
	}
	else
	{
		path_url = http_request->URI_path;
	}

	str_replace_first(&http_response->body, "$URL", html_special_chars_escape(&path_url).c_str());

	if (http_request->headers.find("host") == http_request->headers.end())
	{
		str_replace_first(&http_response->body, "$HOSTNAME", &SERVER_CONFIGURATION["default_host"]);
	}
	else
	{
		str_replace_first(&http_response->body, "$HOSTNAME", &http_request->headers["host"]);
	}

	if (!conn->https)
	{
		str_replace_first(&http_response->body, "$SSL_INFO", "");
	}
#ifndef DISABLE_HTTPS
	else
	{
		std::string ssl_info = "(";
		ssl_info.append(OPENSSL_VERSION_TEXT);
		ssl_info.append(")");
		str_replace_first(&http_response->body, "$SSL_INFO", &ssl_info);
	}
#endif

	http_response->headers["content-type"] = "text/html; charset=utf-8";
	http_response->headers["accept-ranges"] = "none";
	http_response->headers["content-length"] = int2str(http_response->body.size());

	auto last_modified_header_p = http_response->headers.find("last-modified");
	if (last_modified_header_p != http_response->headers.end())
	{
		http_response->headers.erase(last_modified_header_p);
	}

	SERVER_LOG_REQUEST(conn, stream_id);

	if(conn->http_version == HTTP_VERSION_2)
	{
		current_stream->state = HTTP2_STREAM_STATE_SEND_HEADERS;
	}
	else
	{
		conn->state = HTTP_STATE_CONTENT_BOUND;
	}

	return HTTP_Request_Send_Response(worker_id, conn, stream_id);
}

int HTTP_Generate_Folder_Response(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id, const std::string *full_path)
{
	struct HTTP1_CONNECTION *http1_conn = NULL; 

	struct HTTP2_CONNECTION *http2_conn = NULL;
	struct HTTP2_STREAM *current_stream = NULL;

	struct HTTP_REQUEST *http_request = NULL;
	struct HTTP_RESPONSE *http_response = NULL;
	
	if(conn->http_version == HTTP_VERSION_2)
	{
		http2_conn = (struct HTTP2_CONNECTION*) conn->raw_connection;
		current_stream = &http2_conn->streams[stream_id];
		http_request = &current_stream->request;
		http_response = &current_stream->response;
	}
	else
	{
		http1_conn = (struct HTTP1_CONNECTION*) conn->raw_connection;
		http_request = &http1_conn->request;
		http_response = &http1_conn->response;
	}

	struct dirent *dir_entry;
	DIR *folder = opendir(full_path->c_str());

	if (folder == NULL)
	{
		return HTTP_Request_Set_Error_Page(worker_id, conn, stream_id, 500);
	}

	http_response->code = 200;
	http_response->headers["content-type"] = "text/html; charset=utf-8";
	http_response->headers["accept-ranges"] = "none";

	std::string directory_listing_content;
	std::string top_title = "<p style=\"position:relative; font-size:125%; left:1%; width:98%; border-bottom:2px solid gray;\">Directory listing of <span style=\"font-style:italic;\">";

	if (http_request->URI_path.size() > 128)
	{
		std::string short_url = http_request->URI_path.substr(0, 125);
		short_url.append("...");
		top_title.append(html_special_chars_escape(&short_url));
	}
	else
	{
		top_title.append(html_special_chars_escape(&http_request->URI_path));
	}

	top_title.append("</span></p>");

	std::string file_link_base = http_request->URI_path;

	file_link_base = rectify_path(&file_link_base);
	if (file_link_base[file_link_base.size() - 1] != '/')
	{
		file_link_base.append(1, '/');
	}

	directory_listing_content.append("<p><a href=\"");
	directory_listing_content.append(std::string(file_link_base).append(".."));
	directory_listing_content.append("\">..</a></p>\n");

	size_t num_links = 1;
	while ((dir_entry = readdir(folder)) != NULL)
	{
		if (num_links == 128)
		{
			directory_listing_content.append("<p>...</p>\n");
			break;
		}

		if (strcmp(".", dir_entry->d_name) == 0 or strcmp("..", dir_entry->d_name) == 0 or strcmp(".access_config", dir_entry->d_name) == 0)
		{
			continue;
		}

		std::string file_link = file_link_base;
		file_link.append(dir_entry->d_name);

		directory_listing_content.append("<p><a href=\"");
		directory_listing_content.append(file_link);
		directory_listing_content.append("\">");

		if (strlen(dir_entry->d_name) < 128)
		{
			directory_listing_content.append(html_special_chars_escape(dir_entry->d_name));
		}

		else
		{
			std::string trimmed_d_name = dir_entry->d_name;
			trimmed_d_name = trimmed_d_name.substr(0, 125);
			trimmed_d_name.append("...");
			directory_listing_content.append(html_special_chars_escape(&trimmed_d_name));
		}

		directory_listing_content.append("</a></p>\n");
		num_links++;
	}

	closedir(folder);

	std::string footer = SERVER_CONFIGURATION["server_name"];
	footer.append("/");
	footer.append(SERVER_CONFIGURATION["server_version"]);
	footer.append(" (");
	footer.append(SERVER_CONFIGURATION["os_name"]);
	footer.append("/");
	footer.append(SERVER_CONFIGURATION["os_version"]);
	footer.append(") on ");

	if (http_request->headers.find("host") == http_request->headers.end())
	{
		footer.append(SERVER_CONFIGURATION["default_host"]);
	}
	else
	{
		footer.append(http_request->headers["host"]);
	}

	footer.append(" port ");
	footer.append(int2str(conn->server_port).c_str());

#ifndef DISABLE_HTTPS
	if (conn->https)
	{
		footer.append(" (");
		footer.append(OPENSSL_VERSION_TEXT);
		footer.append(")");
	}
#endif

	http_response->body = SERVER_DIRECTORY_LISTING_TEMPLATE;

	str_replace_first(&http_response->body, "$top_title", &top_title);
	str_replace_first(&http_response->body, "$dir_content", &directory_listing_content);
	str_replace_first(&http_response->body, "$footer", &footer);

	http_response->headers["content-length"] = int2str(http_response->body.size());

	if (http_request->method == HTTP_METHOD_HEAD)
	{
		http_response->body.clear();
	}

	SERVER_LOG_REQUEST(conn, stream_id);

	if(conn->http_version == HTTP_VERSION_2)
	{
		current_stream->state = HTTP2_STREAM_STATE_SEND_HEADERS;
	}
	else
	{
		conn->state = HTTP_STATE_CONTENT_BOUND;
	}

	return HTTP_Request_Send_Response(worker_id, conn, stream_id);
}

int HTTP_Request_Host_Get(struct HTTP_REQUEST *request, struct HTTP_RESPONSE *response, bool strict_hosts, std::string* real_hostname)
{
	if (request->headers.find("host") == request->headers.end())
	{
		SERVER_LOG_WRITE_ERROR.lock();
		SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING), true);
		SERVER_LOG_WRITE(" The request doesn't have a host header!\n\n", true);
		SERVER_LOG_WRITE_ERROR.unlock();

		if(strict_hosts)
		{
			return false;
		}

		request->headers["host"] = SERVER_CONFIGURATION["default_host"];
	}
	
	*real_hostname = request->headers["host"];

	if (SERVER_HOSTNAMES.find(*real_hostname) == SERVER_HOSTNAMES.end()) // bad host
	{
		if (strict_hosts)
		{
			SERVER_LOG_WRITE_ERROR.lock();
			SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING), true);
			SERVER_LOG_WRITE(" The requested host is not defined in the hosts list!\n\n", true);
			SERVER_LOG_WRITE_ERROR.unlock();

			return false;
		}
		else
		{
			*real_hostname = SERVER_CONFIGURATION["default_host"];
		}
	}
	
	return true;
}

int HTTP_Request_Connection_Header_Process(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, struct HTTP_REQUEST *request, struct HTTP_RESPONSE *response)
{
	if (conn->http_version == HTTP_VERSION_1 or conn->http_version == HTTP_VERSION_1_1)
	{
		auto connection_header = request->headers.find("connection");
		auto upgrade_header = request->headers.find("upgrade");
		auto http2_settings_header = request->headers.find("http2-settings");

		if (connection_header == request->headers.end())
		{
			response->headers["connection"] = "close";
			return HTTP_CONNECTION_OK;
		}

		std::string connection_header_val = str_ansi_to_lower(&connection_header->second); //TBD for requests with body
		if (connection_header_val.find("upgrade") != std::string::npos and upgrade_header != request->headers.end() and http2_settings_header != request->headers.end())
		{
			std::string upgrade_header_val = str_ansi_to_lower(&upgrade_header->second);

			std::string upgrade_protocol;
			if (upgrade_header_val.find("h2c") != std::string::npos)
			{
				upgrade_protocol = "h2c";
			}
			else if (upgrade_header_val.find("HTTP/2") != std::string::npos)
			{
				upgrade_protocol = "HTTP/2";
			}
			else if (upgrade_header_val.find("HTTP/2.0") != std::string::npos)
			{
				upgrade_protocol = "HTTP/2.0";
			}
			else if (upgrade_header_val.find("h2") != std::string::npos and conn->https)
			{
				upgrade_protocol = "h2";
			}

			if (!upgrade_protocol.empty())
			{
				response->code = 101;

				response->headers["connection"] = "upgrade";
				response->headers["upgrade"] = upgrade_protocol;

				SERVER_LOG_REQUEST(conn, 0);
				return HTTP_Request_Send_Response(worker_id, conn, 0);
			}
		}
		else if (connection_header_val.find("keep-alive") != std::string::npos)
		{
			response->headers["connection"] = "keep-alive";

			std::string keep_alive_value = "timeout=";
			keep_alive_value.append(SERVER_CONFIGURATION["request_timeout"]);

			response->headers["keep-alive"] = keep_alive_value;
		}
		else
		{
			response->headers["connection"] = "close";
		}
	}

	return HTTP_CONNECTION_OK;
}

int HTTP_Request_Process(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id)
{
	struct HTTP1_CONNECTION *http1_conn = NULL; 

	struct HTTP2_CONNECTION *http2_conn = NULL;
	struct HTTP2_STREAM *current_stream = NULL;

	struct HTTP_REQUEST *http_request = NULL;
	struct HTTP_RESPONSE *http_response = NULL;
	struct HTTP_FILE_TRANSFER *http_file_transfer = NULL;
	
	if(conn->http_version == HTTP_VERSION_2)
	{
		http2_conn = (struct HTTP2_CONNECTION*) conn->raw_connection;
		current_stream = &http2_conn->streams[stream_id];

		http_request = &current_stream->request;
		http_response = &current_stream->response;
		http_file_transfer = &current_stream->file_transfer;
	}
	else
	{
		http1_conn = (struct HTTP1_CONNECTION*) conn->raw_connection;

		http_request = &http1_conn->request;
		http_response = &http1_conn->response;
		http_file_transfer = &http1_conn->file_transfer;
	}

	if ((conn->http_version == HTTP_VERSION_2 and current_stream->state == HTTP2_STREAM_STATE_PROCESSING) or 
	    (conn->http_version != HTTP_VERSION_2 and conn->state == HTTP_STATE_PROCESSING))
	{
		http_response->headers["server"] = SERVER_CONFIGURATION["server_name"];
		http_response->headers["server"].append(1, '/');
		http_response->headers["server"].append(SERVER_CONFIGURATION["server_version"]);

		http_response->headers["date"] = convert_ctime2_http_date(time(NULL));

		bool strict_hosts = true;
		if (is_server_config_variable_false("strict_hosts"))
		{
			strict_hosts = false;
		}

		std::string real_hostname;
		if(!HTTP_Request_Host_Get(http_request, http_response, strict_hosts, &real_hostname))
		{
			return HTTP_Request_Set_Error_Page(worker_id, conn, stream_id, 400);
		}

		if(HTTP_Request_Connection_Header_Process(worker_id, conn, http_request, http_response) == HTTP_CONNECTION_DELETED)
		{
			return HTTP2_CONNECTION_DELETED;
		}

		http_response->headers["host"] = http_request->headers["host"];

		std::string relative_path = rectify_path(&http_request->URI_path);
        std::string full_path = SERVER_HOSTNAMES[real_hostname];   
        full_path.append(1,'/');
        full_path.append(relative_path);
        full_path = rectify_path(&full_path);
		
		int check_file_code = check_file_access(worker_id, relative_path, SERVER_HOSTNAMES[real_hostname]);
		if (check_file_code != 0)
		{
			return HTTP_Request_Set_Error_Page(worker_id, conn, stream_id, check_file_code);	
		}

		struct custom_bound_entry custom_page_generator;
		if (check_custom_bound_path(full_path, &custom_page_generator))
		{
			//parse cookies
			HTTP_Parse_Cookie_Header(http_request->headers["cookie"], &http_request->COOKIES);

			//parse the body
			if(HTTP_Parse_Request_Body(worker_id, conn, stream_id, http_request, http_response))
			{
				return HTTP2_CONNECTION_DELETED;
			}
			
			if (conn->http_version == HTTP_VERSION_2)
			{
				current_stream->state = HTTP2_STREAM_STATE_CUSTOM_BOUND;
			}
			else
			{
				conn->state = HTTP_STATE_CUSTOM_BOUND;
			}

			http_response->code = 200;
			http_response->headers["content-type"] = "text/html; charset=utf-8";

			return run_custom_page_generator(worker_id, conn, stream_id, &custom_page_generator);
		}

		if (http_request->method != HTTP_METHOD_GET and http_request->method != HTTP_METHOD_HEAD)
		{
			SERVER_LOG_WRITE_ERROR.lock();
			SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING), true);
			SERVER_LOG_WRITE(" The requested method is not supported on this resource!\n\n", true);
			SERVER_LOG_WRITE_ERROR.unlock();

			return HTTP_Request_Set_Error_Page(worker_id, conn, stream_id, 405);
		}

		uint64_t requested_file_size;
		time_t requested_file_mdate;
		bool is_folder;
		if (get_file_info(&full_path, &requested_file_size, &requested_file_mdate, &is_folder) == -1) // get file size and modification date
		{
			return HTTP_Request_Set_Error_Page(worker_id, conn, stream_id, 404);
		}

		if (is_folder)
		{
			return HTTP_Generate_Folder_Response(worker_id, conn, stream_id, &full_path);
		}

		http_response->headers["last-modified"] = convert_ctime2_http_date(requested_file_mdate);
		http_response->headers["content-type"] = get_MIME_type_by_ext(&full_path);
		http_response->headers["accept-ranges"] = "bytes";

		if (http_request->headers.find("if-modified-since") != http_request->headers.end())
		{
			time_t mod_time;
			if (!convert_http_date2_ctime(&http_request->headers["if-modified-since"], &mod_time))
			{
				SERVER_LOG_WRITE_ERROR.lock();
				SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING), true);
				SERVER_LOG_WRITE(" The request If-Modified-Since header can't be parsed!\n\n", true);
				SERVER_LOG_WRITE_ERROR.unlock();

				return HTTP_Request_Set_Error_Page(worker_id, conn, stream_id, 400);
			}

			if (requested_file_mdate <= mod_time)
			{
				http_response->code = 304;
				
				if(conn->http_version == HTTP_VERSION_2)
				{
					current_stream->state = HTTP2_STREAM_STATE_SEND_HEADERS;
				}
				else
				{
					conn->state = HTTP_STATE_CONTENT_BOUND;
				}

				SERVER_LOG_REQUEST(conn, stream_id);
				return HTTP_Request_Send_Response(worker_id, conn, stream_id);
			}
		}

		if (http_request->headers.find("range") != http_request->headers.end())
		{
			int64_t req_start, req_stop;
			if (!HTTP_Decode_Content_Range(http_request->headers["range"], &req_start, &req_stop))
			{
				SERVER_LOG_WRITE_ERROR.lock();
				SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING), true);
				SERVER_LOG_WRITE(" The request Range header can't be parsed!\n\n", true);
				SERVER_LOG_WRITE_ERROR.unlock();

				return HTTP_Request_Set_Error_Page(worker_id, conn, stream_id, 416);
			}

			// some browsers specifiy only the starting offset
			if (req_stop == -1)
			{
				req_stop = requested_file_size;
			}

			if (req_start >= req_stop or (uint64_t) req_stop > requested_file_size)
			{
				SERVER_LOG_WRITE_ERROR.lock();
				SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING), true);
				SERVER_LOG_WRITE(" The request Range header is not valid!\n\n", true);
				SERVER_LOG_WRITE_ERROR.unlock();

				return HTTP_Request_Set_Error_Page(worker_id, conn, stream_id, 416);
			}

			http_response->code = 206;

			http_response->headers["content-range"] = HTTP_Encode_Content_Range(req_start, req_stop, requested_file_size);
			http_response->headers["content-length"] = int2str(req_stop - req_start);

			http_file_transfer->file_offset = req_start;
			http_file_transfer->stop_offset = req_stop;
		}
		else 
		{
			http_response->code = 200;

			http_response->headers["content-length"] = int2str(requested_file_size);

			http_file_transfer->file_offset = 0;
			http_file_transfer->stop_offset = requested_file_size;
		}

		if (http_request->method == HTTP_METHOD_HEAD)
		{
			if (conn->http_version == HTTP_VERSION_2)
			{
				current_stream->state = HTTP2_STREAM_STATE_SEND_HEADERS;
			}
			else
			{
				conn->state = HTTP_STATE_CONTENT_BOUND;
			}

			SERVER_LOG_REQUEST(conn, stream_id);
			return HTTP_Request_Send_Response(worker_id, conn, stream_id);
		}

		http_file_transfer->file_descriptor = open(full_path.c_str(), O_RDONLY | O_CLOEXEC);
		if (http_file_transfer->file_descriptor == -1)
		{
			std::string error_msg = "The server is unable to open the following resource!\nPath: ";
			error_msg.append(full_path);

			SERVER_ERROR_LOG_stdlib_err(error_msg.c_str());

			return HTTP_Request_Set_Error_Page(worker_id, conn, stream_id, 500);
		}

		if (conn->http_version == HTTP_VERSION_2)
		{
			current_stream->state = HTTP2_STREAM_STATE_FILE_BOUND;
		}
		else
		{
			conn->state = HTTP_STATE_FILE_BOUND;
		}

		SERVER_LOG_REQUEST(conn, stream_id);
		return HTTP_Request_Send_Response(worker_id, conn, stream_id);
	}

	else if (conn->http_version == HTTP_VERSION_2 and current_stream->state == HTTP2_STREAM_STATE_CONTENT_BOUND)
	{
		HTTP2_Stream_Send_Body(worker_id, http2_conn, stream_id);
	}

	else if (conn->http_version == HTTP_VERSION_2 and current_stream->state == HTTP2_STREAM_STATE_FILE_BOUND)
	{
		return HTTP2_Stream_Send_From_File(worker_id, conn, stream_id);
	}

	return HTTP2_CONNECTION_OK;
}
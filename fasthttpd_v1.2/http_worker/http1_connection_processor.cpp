#include "http_worker.h"
#include "http1_connection_processor.h"
#include "http_parser.h"

#include "../server_config.h"
#include "../server_log.h"
#include "../helper_functions.h"

#include <unistd.h>
#include <cstring>

int HTTP1_Connection_Read_Incoming_Data(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn)
{
	struct HTTP1_CONNECTION *http_conn = (struct HTTP1_CONNECTION *)conn->raw_connection;

	uint32_t recv_buff_size = str2uint(SERVER_CONFIGURATION["read_buffer_size"]);
    uint64_t max_req_size = str2uint(SERVER_CONFIGURATION["max_request_size"]) * 1024;

	bool should_stop = false;
	while (!should_stop)
	{
		int32_t read_bytes = Network_Read_Bytes(conn, http_workers[worker_id].recv_buffer, recv_buff_size);
		if (read_bytes < 0)
		{
			Generic_Connection_Delete(worker_id, conn);
			return HTTP_CONNECTION_DELETED;
		}

		if (read_bytes == 0)
		{
			should_stop = true;
			continue;
		}

		http_conn->recv_buffer.append(http_workers[worker_id].recv_buffer, read_bytes);

		if(http_conn->recv_buffer.size() > max_req_size)
        {
            return HTTP_Request_Set_Error_Page(worker_id, conn, 0, 413);
        }
	}

    return HTTP_CONNECTION_OK;
}

int HTTP1_Connection_Send_Data(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn)
{
	struct HTTP1_CONNECTION *http_conn = (struct HTTP1_CONNECTION *)conn->raw_connection;

	bool should_stop = false;
	while (!should_stop)
	{
		int32_t send_bytes = Network_Write_Bytes(conn, (void*)http_conn->send_buffer.c_str(), http_conn->send_buffer.size() - http_conn->send_buffer_offset);
		if (send_bytes < 0)
		{
			Generic_Connection_Delete(worker_id, conn);
			return HTTP_CONNECTION_DELETED;
		}

		if (send_bytes == 0)
		{
			should_stop = true;
			continue;
		}

		http_conn->send_buffer_offset += send_bytes;

		if(http_conn->send_buffer_offset == http_conn->send_buffer.size())
		{
            if(conn->state == HTTP_STATE_FILE_BOUND)
            {
                if(http_conn->file_transfer.file_offset != http_conn->file_transfer.stop_offset)
                {
                    if(HTTP1_Connection_Load_File_Chunk(worker_id, conn) == HTTP_CONNECTION_DELETED)
                    {
                        return HTTP_CONNECTION_DELETED;
                    }

                    continue;
                }
            }

            auto connection_header = http_conn->response.headers.find("connection");
            if(connection_header != http_conn->response.headers.end())
            {
                if(connection_header->second == "upgrade")
                {   
                    HTTP1_Connection_HTTP2_101_Upgrade(worker_id, conn);
                    return HTTP_CONNECTION_DELETED;
                }

                else if(connection_header->second == "keep-alive")
                {
                    HTTP1_Connection_Delete(http_conn);
                    conn->state = HTTP_STATE_WAIT_PATH;
                    return HTTP_CONNECTION_OK;
                }

            }
            
            Generic_Connection_Delete(worker_id, conn);
		    return HTTP_CONNECTION_DELETED;
		}
	}

	return HTTP_CONNECTION_OK;
}

int HTTP1_Connection_Load_File_Chunk(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn)
{
    struct HTTP1_CONNECTION *http_conn = (struct HTTP1_CONNECTION *)conn->raw_connection;

    uint32_t max_read_size = str2uint(SERVER_CONFIGURATION["read_buffer_size"]) * 1024;

    uint64_t remaining_bytes = http_conn->file_transfer.stop_offset - http_conn->file_transfer.file_offset;
    uint64_t bytes_to_read = (remaining_bytes > max_read_size) ? max_read_size : remaining_bytes;

    ssize_t read_bytes;

    bool should_stop = false;

    while (!should_stop)
    {
        read_bytes = pread(http_conn->file_transfer.file_descriptor, http_workers[worker_id].recv_buffer, bytes_to_read, http_conn->file_transfer.file_offset);

        if (read_bytes == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            else
            {
                std::string err_msg = "Unable to read from requested file (";
                err_msg.append(http_conn->request.URI_path);
                err_msg.append(" )");

                SERVER_ERROR_LOG_stdlib_err(err_msg.c_str());

                Generic_Connection_Delete(worker_id, conn);
                return HTTP_CONNECTION_DELETED;
            }
        }

        should_stop = true;
    }

    http_conn->file_transfer.file_offset += read_bytes;
    http_conn->send_buffer_offset = 0;
    http_conn->send_buffer = std::string(http_workers[worker_id].recv_buffer, read_bytes);

    return HTTP_CONNECTION_OK;
}

void HTTP1_Connection_Init(struct HTTP1_CONNECTION *http_conn)
{
    http_conn->send_buffer_offset = 0;

    http_conn->request.COOKIES = NULL;
    http_conn->request.POST_query = NULL;
    http_conn->request.POST_files = NULL;
    http_conn->request.POST_type = HTTP_POST_TYPE_UNDEFINED;

    http_conn->response.COOKIES = NULL;

    http_conn->file_transfer.file_descriptor = -1;
}

void HTTP1_Connection_HTTP2_101_Upgrade(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn)
{
    struct HTTP1_CONNECTION *http_conn = (struct HTTP1_CONNECTION *)conn->raw_connection;

    struct HTTP2_CONNECTION *http2_conn = new (std::nothrow) struct HTTP2_CONNECTION();
    if (!http2_conn)
    {
        SERVER_ERROR_LOG_stdlib_err("Unable to allocate memory for a HTTP2_CONNECTION.");
        exit(-1);
    }

    HTTP2_Connection_Init(http2_conn);

    conn->raw_connection = http2_conn;
    conn->http_version = HTTP_VERSION_2;
    conn->state = HTTP2_CONNECTION_STATE_WAIT_HELLO;


    //reassemble the HTTP/1.1 request on stream 1
    if(HTTP2_Stream_Init(worker_id, conn, 1) == HTTP2_CONNECTION_DELETED)
    {
        //free the HTTP/1.1 connection data
        HTTP1_Connection_Delete(http_conn);
        delete (http_conn);

        return;
    }
    
    struct HTTP_REQUEST& request = http2_conn->streams[1].request;
    request.method = http_conn->request.method;
    request.URI_path = http_conn->request.URI_path;
    request.URI_query = http_conn->request.URI_query;
    request.headers = http_conn->request.headers;

    auto connection_header_it = request.headers.find("connection");
    auto upgrade_header_it = request.headers.find("upgrade");
    auto http2_settings_header_it = request.headers.find("http2-settings");

    //free the HTTP/1.1 connection data
    HTTP1_Connection_Delete(http_conn);
    delete (http_conn);

    //remove HTTP/1.1 specific headers
    if(connection_header_it != request.headers.end())
    {
        request.headers.erase(connection_header_it);
    }
    if(upgrade_header_it != request.headers.end())
    {
        request.headers.erase(upgrade_header_it);
    }
    if(http2_settings_header_it != request.headers.end())
    {
        request.headers.erase(http2_settings_header_it);
    }

    http2_conn->streams[1].state = HTTP2_STREAM_STATE_PROCESSING;
    if(HTTP_Request_Process(worker_id, conn, 1) == HTTP2_CONNECTION_DELETED)
    {
        return;
    }

    HTTP2_Connection_Process(worker_id, conn);
}

void HTTP1_Connection_HTTP2_Upgrade_Directly(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn)
{
    struct HTTP1_CONNECTION *http_conn = (struct HTTP1_CONNECTION *)conn->raw_connection;

    struct HTTP2_CONNECTION *http2_conn = new (std::nothrow) struct HTTP2_CONNECTION();
    if (!http2_conn)
    {
        SERVER_ERROR_LOG_stdlib_err("Unable to allocate memory for a HTTP2_CONNECTION.");
        exit(-1);
    }

    HTTP2_Connection_Init(http2_conn);
    HTTP2_Frame_Settings_Generate(http2_conn);

    std::string rest_of_data;
    if (http_conn->recv_buffer.size() > 24)
    {
        rest_of_data = http_conn->recv_buffer.substr(24);
    }

    HTTP1_Connection_Delete(http_conn);
    delete (http_conn);

    conn->raw_connection = http2_conn;
    conn->http_version = HTTP_VERSION_2;
    conn->state = HTTP2_CONNECTION_STATE_WAIT_SETTINGS;

    if (!rest_of_data.empty())
    {
        HTTP2_Connection_Process(worker_id, conn, &rest_of_data);
    }
    else
    {
        HTTP2_Connection_Process(worker_id, conn);
    }
}

void HTTP1_Connection_Process(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn)
{
    struct HTTP1_CONNECTION *http_conn = (struct HTTP1_CONNECTION *)conn->raw_connection;

    if (conn->state == HTTP_STATE_WAIT_PATH or conn->state == HTTP_STATE_WAIT_HEADERS or conn->state == HTTP_STATE_WAIT_BODY)
    {
        if(HTTP1_Connection_Read_Incoming_Data(worker_id, conn) == HTTP_CONNECTION_DELETED)
        {
            return;
        }
    }

    if (conn->state == HTTP_STATE_WAIT_PATH and http_conn->recv_buffer.find("\n") != std::string::npos)
    {
        // extract the path, the http version and the method
        std::string raw_URI;
        int http_ver;
        int method;

        if (!HTTP_Parse_Request_First_Line(http_conn->recv_buffer, &raw_URI, &method, &http_ver, &http_conn->parser_helper.headers_start_offset))
        {
            HTTP_Request_Set_Error_Page(worker_id, conn, 0, 400);
            return;
        }

        if (http_ver == HTTP_VERSION_1)
        {
            conn->http_version = HTTP_VERSION_1;
        }
        else if (http_ver == HTTP_VERSION_1_1)
        {
            conn->http_version = HTTP_VERSION_1_1;
        }
        else if (http_ver == HTTP_VERSION_2 and memcmp(http_conn->recv_buffer.c_str(), HTTP2_magic_hello, 24) == 0)
        {
            HTTP1_Connection_HTTP2_Upgrade_Directly(worker_id, conn);
            return;
        }
        else
        {
            HTTP_Request_Set_Error_Page(worker_id, conn, 0, 400);
            return;
        }

        if (method == HTTP_METHOD_UNDEFINED)
        {
            HTTP_Request_Set_Error_Page(worker_id, conn, 0, 400);
            return;
        }
        else
        {
            http_conn->request.method = method;
        }

        unsigned int max_query_arg_limit = str2uint(&SERVER_CONFIGURATION["max_query_args"]);

        bool continue_if_arg_limit_exceeded = false;
        if (is_server_config_variable_true("continue_if_args_limit_exceeded"))
        {
            continue_if_arg_limit_exceeded = true;
        }

        if (!HTTP_Parse_Raw_URI(raw_URI, &http_conn->request.URI_path, &http_conn->request.URI_query, max_query_arg_limit, continue_if_arg_limit_exceeded))
        {  
            HTTP_Request_Set_Error_Page(worker_id, conn, 0, 400);
            return;
        }

        conn->state = HTTP_STATE_WAIT_HEADERS;
    }

    if (conn->state == HTTP_STATE_WAIT_HEADERS and (http_conn->recv_buffer.find("\r\n\r\n", http_conn->parser_helper.headers_start_offset) != std::string::npos 
                                                    or http_conn->recv_buffer.find("\n\n", http_conn->parser_helper.headers_start_offset) != std::string::npos))
    {
        if (!HTTP_Parse_Request_Headers(http_conn->recv_buffer, http_conn->parser_helper.headers_start_offset, &http_conn->request.headers, &http_conn->parser_helper.body_start_offset))
        {
            HTTP_Request_Set_Error_Page(worker_id, conn, 0, 400);
            return;
        }

        if (http_conn->request.method == HTTP_METHOD_POST or http_conn->request.method == HTTP_METHOD_PUT)
        {
            // parse the content length and check the validity
            auto content_len_header = http_conn->request.headers.find("content-length");
            if (content_len_header == http_conn->request.headers.end())
            {
                HTTP_Request_Set_Error_Page(worker_id, conn, 0, 411);
                return;
            }

            bool invalid_num = true;
            http_conn->parser_helper.content_length = str2uint(content_len_header->second, &invalid_num);

            if (invalid_num)
            {
                HTTP_Request_Set_Error_Page(worker_id, conn, 0, 400);
                return;
            }

            conn->state = HTTP_STATE_WAIT_BODY;
        }
        else
        {
            http_conn->recv_buffer.clear();
            conn->state = HTTP_STATE_PROCESSING;
            HTTP_Request_Process(worker_id, conn, 0);
            return;
        }
    }
    if (conn->state == HTTP_STATE_WAIT_BODY)
    {  
        uint64_t full_request_len = http_conn->parser_helper.body_start_offset + http_conn->parser_helper.content_length;

        if(full_request_len > str2uint(SERVER_CONFIGURATION["max_request_size"]) * 1024)
        {
            HTTP_Request_Set_Error_Page(worker_id, conn, 0, 413);
            return;
        }

        if(http_conn->recv_buffer.size() == http_conn->parser_helper.body_start_offset + http_conn->parser_helper.content_length)
        {
            conn->state = HTTP_STATE_PROCESSING;
            HTTP_Request_Process(worker_id, conn, 0);
            return;
        }
    }

    if (conn->state == HTTP_STATE_CONTENT_BOUND or conn->state == HTTP_STATE_FILE_BOUND)
    {
        HTTP1_Connection_Send_Data(worker_id, conn);
    }
}

void HTTP1_Connection_Generate_Response(struct GENERIC_HTTP_CONNECTION *conn)
{
    struct HTTP1_CONNECTION *http_conn = (struct HTTP1_CONNECTION *)conn->raw_connection;

    if (conn->http_version == HTTP_VERSION_1)
    {
        http_conn->send_buffer = "HTTP/1.0 ";
    }
    else
    {
        http_conn->send_buffer = "HTTP/1.1 ";
    }

    http_conn->send_buffer.append(int2str(http_conn->response.code));
    http_conn->send_buffer.append(" ");

    if (http_conn->response.code == 100)
    {
        http_conn->send_buffer.append("Continue");
    }
    else if (http_conn->response.code == 101)
    {
        http_conn->send_buffer.append("Switching Protocols");
    }
    else if (http_conn->response.code == 102)
    {
        http_conn->send_buffer.append("Processing");
    }
    else if (http_conn->response.code == 200)
    {
        http_conn->send_buffer.append("OK");
    }
    else if (http_conn->response.code == 201)
    {
        http_conn->send_buffer.append("Created");
    }
    else if (http_conn->response.code == 202)
    {
        http_conn->send_buffer.append("Accepted");
    }
    else if (http_conn->response.code == 203)
    {
        http_conn->send_buffer.append("Non-Authoritative Information");
    }
    else if (http_conn->response.code == 204)
    {
        http_conn->send_buffer.append("No Content");
    }
    else if (http_conn->response.code == 205)
    {
        http_conn->send_buffer.append("Reset Content");
    }
    else if (http_conn->response.code == 206)
    {
        http_conn->send_buffer.append("Partial Content");
    }
    else if (http_conn->response.code == 300)
    {
        http_conn->send_buffer.append("Multiple Choices");
    }
    else if (http_conn->response.code == 301)
    {
        http_conn->send_buffer.append("Moved Permanently");
    }
    else if (http_conn->response.code == 302)
    {
        http_conn->send_buffer.append("Found");
    }
    else if (http_conn->response.code == 303)
    {
        http_conn->send_buffer.append("See Other");
    }
    else if (http_conn->response.code == 304)
    {
        http_conn->send_buffer.append("Not Modified");
    }
    else if (http_conn->response.code == 305)
    {
        http_conn->send_buffer.append("Use Proxy");
    }
    else if (http_conn->response.code == 306)
    {
        http_conn->send_buffer.append("Unused");
    }
    else if (http_conn->response.code == 307)
    {
        http_conn->send_buffer.append("Temporary Redirect");
    }
    else if (http_conn->response.code == 308)
    {
        http_conn->send_buffer.append("Permanent Redirect");
    }
    else if (http_conn->response.code == 400)
    {
        http_conn->send_buffer.append("Bad Request");
    }
    else if (http_conn->response.code == 401)
    {
        http_conn->send_buffer.append("Unauthorized");
    }
    else if (http_conn->response.code == 402)
    {
        http_conn->send_buffer.append("Payment Required");
    }
    else if (http_conn->response.code == 403)
    {
        http_conn->send_buffer.append("Forbidden");
    }
    else if (http_conn->response.code == 404)
    {
        http_conn->send_buffer.append("Not Found");
    }
    else if (http_conn->response.code == 405)
    {
        http_conn->send_buffer.append("Method Not Allowed");
    }
    else if (http_conn->response.code == 406)
    {
        http_conn->send_buffer.append("Not Acceptable");
    }
    else if (http_conn->response.code == 407)
    {
        http_conn->send_buffer.append("Proxy Authentication Required");
    }
    else if (http_conn->response.code == 408)
    {
        http_conn->send_buffer.append("Request Timeout");
    }
    else if (http_conn->response.code == 409)
    {
        http_conn->send_buffer.append("Conflict");
    }
    else if (http_conn->response.code == 410)
    {
        http_conn->send_buffer.append("Gone");
    }
    else if (http_conn->response.code == 411)
    {
        http_conn->send_buffer.append("Length Required");
    }
    else if (http_conn->response.code == 412)
    {
        http_conn->send_buffer.append("Precondition Failed");
    }
    else if (http_conn->response.code == 413)
    {
        http_conn->send_buffer.append("Request Entity Too Large");
    }
    else if (http_conn->response.code == 414)
    {
        http_conn->send_buffer.append("Request-URI Too Long");
    }
    else if (http_conn->response.code == 415)
    {
        http_conn->send_buffer.append("Unsupported Media Type");
    }
    else if (http_conn->response.code == 416)
    {
        http_conn->send_buffer.append("Requested Range Not Satisfiable");
    }
    else if (http_conn->response.code == 417)
    {
        http_conn->send_buffer.append("Expectation Failed");
    }
    else if (http_conn->response.code == 418)
    {
        http_conn->send_buffer.append("I'm a teapot");
    }
    else if (http_conn->response.code == 422)
    {
        http_conn->send_buffer.append("Unprocessable Entity");
    }
    else if (http_conn->response.code == 428)
    {
        http_conn->send_buffer.append("Precondition Required");
    }
    else if (http_conn->response.code == 429)
    {
        http_conn->send_buffer.append("Too Many Requests");
    }
    else if (http_conn->response.code == 431)
    {
        http_conn->send_buffer.append("Request Header Fields Too Large");
    }
    else if (http_conn->response.code == 451)
    {
        http_conn->send_buffer.append("Unavailable For Legal Reasons");
    }
    else if (http_conn->response.code == 500)
    {
        http_conn->send_buffer.append("Internal Server Error");
    }
    else if (http_conn->response.code == 501)
    {
        http_conn->send_buffer.append("Not Implemented");
    }
    else if (http_conn->response.code == 502)
    {
        http_conn->send_buffer.append("Bad Gateway");
    }
    else if (http_conn->response.code == 503)
    {
        http_conn->send_buffer.append("Service Unavailable");
    }
    else if (http_conn->response.code == 504)
    {
        http_conn->send_buffer.append("Gateway Timeout");
    }
    else if (http_conn->response.code == 505)
    {
        http_conn->send_buffer.append("HTTP Version Not Supported");
    }
    else if (http_conn->response.code == 511)
    {
        http_conn->send_buffer.append("Network Authentication Required");
    }
    else if (http_conn->response.code == 520)
    {
        http_conn->send_buffer.append("Web server is returning an unknown error");
    }
    else if (http_conn->response.code == 522)
    {
        http_conn->send_buffer.append("Connection timed out");
    }
    else if (http_conn->response.code == 524)
    {
        http_conn->send_buffer.append("A timeout occurred");
    }
    else
    {
        http_conn->send_buffer.append("Undefined");
    }

    http_conn->send_buffer.append("\r\n");

    for (auto i = http_conn->response.headers.begin(); i != http_conn->response.headers.end(); ++i)
    {
        http_conn->send_buffer.append(i->first);
        http_conn->send_buffer.append(": ");
        http_conn->send_buffer.append(i->second);
        http_conn->send_buffer.append("\r\n");
    }

    if (http_conn->response.COOKIES)
    {
        for (size_t i = 0; i < http_conn->response.COOKIES->size(); i++)
        {
            http_conn->send_buffer.append("set-cookie: ");
            http_conn->send_buffer.append(HTTP_Generate_Set_Cookie_Header(http_conn->response.COOKIES->at(i)));
            http_conn->send_buffer.append("\r\n");
        }
    }

    http_conn->send_buffer.append("\r\n");
    http_conn->send_buffer.append(http_conn->response.body);
}

void HTTP1_Connection_Delete(struct HTTP1_CONNECTION *http_conn)
{
    http_conn->recv_buffer.clear();
    http_conn->send_buffer.clear();
    http_conn->send_buffer_offset = 0;

    http_conn->request.headers.clear();
    http_conn->request.URI_path.clear();
    http_conn->request.URI_query.clear();

    if (http_conn->request.POST_files)
    {
        delete (http_conn->request.POST_files);
        http_conn->request.POST_files = NULL;
    }

    if (http_conn->request.POST_query)
    {
        delete (http_conn->request.POST_query);
        http_conn->request.POST_query = NULL;
    }

    if (http_conn->request.COOKIES)
    {
        delete (http_conn->request.COOKIES);
        http_conn->request.COOKIES = NULL;
    }

    if (http_conn->file_transfer.file_descriptor != -1)
    {
        close(http_conn->file_transfer.file_descriptor);
    }

    http_conn->file_transfer.file_descriptor = -1;
    http_conn->request.POST_type = HTTP_POST_TYPE_UNDEFINED;

    http_conn->response.headers.clear();
    http_conn->response.body.clear();

    if (http_conn->response.COOKIES)
    {
        delete (http_conn->response.COOKIES);
        http_conn->response.COOKIES = NULL;
    }
}
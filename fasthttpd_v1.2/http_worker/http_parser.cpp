#include "http_parser.h"
#include "http_worker.h"

#include "../server_config.h"
#include "../server_log.h"
#include "../helper_functions.h"

#include <algorithm>
#include <cstring>

int HTTP_Parse_Method(const std::string &method)
{
	if (method == "get" or method == "GET")
	{
		return HTTP_METHOD_GET;
	}
	else if (method == "post" or method == "POST")
	{
		return HTTP_METHOD_POST;
	}
	else if (method == "put" or method == "PUT")
	{
		return HTTP_METHOD_PUT;
	}
	else if (method == "head" or method == "HEAD")
	{
		return HTTP_METHOD_HEAD;
	}
	else if (method == "delete" or method == "DELETE")
	{
		return HTTP_METHOD_DELETE;
	}
	else if (method == "options" or method == "OPTIONS")
	{
		return HTTP_METHOD_OPTIONS;
	}
	else if (method == "connect" or method == "CONNECT")
	{
		return HTTP_METHOD_CONNECT;
	}
	else if (method == "trace" or method == "TRACE")
	{
		return HTTP_METHOD_TRACE;
	}
	else if (method == "patch" or method == "PATCH")
	{
		return HTTP_METHOD_PATCH;
	}

	return HTTP_METHOD_UNDEFINED;
}

bool HTTP_Parse_Request_First_Line(const std::string &first_line, std::string* URI, int* method, int* http_version, size_t* headers_start_offset)
{
    size_t stop_pos = first_line.find('\n');
    if(stop_pos == std::string::npos)
    {
        return false;
    }

    headers_start_offset[0] = stop_pos + 1;

    if(first_line.size() >= 2 and first_line[stop_pos - 1] == '\r')
    {
        stop_pos--;
    }

    size_t first_space = first_line.find(' ');
    if(first_space == std::string::npos or first_space >= stop_pos)
    {
       return false;
    }

    std::string ascii_method = first_line.substr(0, first_space);
    method[0] = HTTP_Parse_Method(ascii_method);

    size_t second_space = first_line.find(' ', first_space + 1);
    if(second_space == std::string::npos or second_space >= stop_pos)
    {
       return false;
    }

    URI[0] = first_line.substr(first_space + 1, second_space - (first_space + 1));

    std::string ascii_http_version = first_line.substr(second_space + 1, stop_pos - (second_space + 1));
    if(ascii_http_version == "HTTP/1.0")
    {
        http_version[0] = HTTP_VERSION_1;
    }
    else if(ascii_http_version == "HTTP/1.1")
    {
        http_version[0] = HTTP_VERSION_1_1;
    }
    else if(ascii_http_version == "HTTP/2.0")
    {
        http_version[0] = HTTP_VERSION_2;
    }
    else
    {
        http_version[0] = HTTP_VERSION_UNDEFINED;
    }

    return true;
}

bool HTTP_Parse_Query(const std::string &query_part, std::unordered_map<std::string, std::string> *query_params, unsigned int max_query_args, bool *query_args_limit_exceeded,
                      bool continue_if_exceeded, int start_offset, int end_offset)
{
    if (!query_params or !query_args_limit_exceeded)
    {
        return false;
    }

    *query_args_limit_exceeded = false;

    std::string query_name, query_value, dec_query_name, dec_query_value;
    bool parser_state = 0; // 0->extracting key ; 1->extracting value

    if (end_offset < 0)
    {
        end_offset = query_part.size() - 1;
    }

    if (start_offset > end_offset)
    {
        return false;
    }

    for (int i = start_offset; i <= end_offset; i++)
    {
        if (query_part[i] == '=')
        {
            parser_state = 1;
        }
        else if (query_part[i] == '&')
        {
            parser_state = 0;

            if (!url_decode(&query_name, &dec_query_name) or !url_decode(&query_value, &dec_query_value))
            {
                return false;
            }

            query_name.clear();
            query_value.clear();

            if (!dec_query_name.empty())
            {
                query_params[0][dec_query_name] = dec_query_value;
            }

            dec_query_name.clear();
            dec_query_value.clear();
        }

        // extracting value
        else if (parser_state)
        {
            query_value.append(1, query_part[i]);
        }

        // extracting key
        else
        {
            query_name.append(1, query_part[i]);
        }
    }

    if (!url_decode(&query_name, &dec_query_name) or !url_decode(&query_value, &dec_query_value))
    {
        return false;
    }

    if (!dec_query_name.empty() and !(*query_args_limit_exceeded))
    {
        if (query_params->size() + 1 > max_query_args)
        {
            *query_args_limit_exceeded = true;

            if (!continue_if_exceeded)
            {
                return false;
            }
        }
        else
        {
            query_params[0][dec_query_name] = dec_query_value;
        }
    }

    return true;
}

bool HTTP_Parse_Raw_URI(const std::string &raw_URI, std::string *URI, std::unordered_map<std::string, std::string> *URI_query_params, unsigned int max_arg_limit, bool continue_if_exceeded)
{
    size_t query_mark = raw_URI.find('?');
    if (query_mark == std::string::npos)
    {
        return url_decode(&raw_URI, URI);
    }

    std::string URI_Path = raw_URI.substr(0, query_mark);
    if (!url_decode(&URI_Path, URI))
    {
        return false;
    }

    bool arg_limit_exceeded;
    bool parse_result = HTTP_Parse_Query(raw_URI, URI_query_params, max_arg_limit, &arg_limit_exceeded, continue_if_exceeded, query_mark + 1);

    if (arg_limit_exceeded)
    {
        SERVER_LOG_WRITE_ERROR.lock();
        SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING), true);
        SERVER_LOG_WRITE(" The number of query arguments exceeded the limit!\n\n", true);
        SERVER_LOG_WRITE_ERROR.unlock();
    }

    return parse_result;
}

bool HTTP_Decode_Content_Range(const std::string &content_range, int64_t *offset_start, int64_t *offset_stop)
{
    std::vector<std::string> content_range_el;
    explode(&content_range, "=", &content_range_el);

    if (content_range_el.size() != 2 or content_range_el[0] != "bytes")
    {
        return false;
    }

    std::vector<std::string> content_range_data;
    explode(&content_range_el[1], "-", &content_range_data);

    if (content_range_data.size() != 2)
    {
        return false;
    }

    bool is_invalid;
    offset_start[0] = (int64_t)str2uint(&content_range_data[0], &is_invalid);

    if (is_invalid)
    {
        return false;
    }

    // some browsers specifiy only the starting offset
    if (content_range_data[1].empty())
    {
        offset_stop[0] = -1;
    }
    else
    {
        offset_stop[0] = (int64_t)str2uint(&content_range_data[1], &is_invalid);

        if (is_invalid)
        {
            return false;
        }
    }

    return true;
}

std::string HTTP_Encode_Content_Range(int64_t offset_start, int64_t offset_stop, int64_t file_size)
{
        std::string result = "bytes ";
        result.append(int2str(offset_start)); result.append(1,'-');
        result.append(int2str(offset_stop)); result.append(1,'/');
        result.append(int2str(file_size));
        return result;
}

bool HTTP_Decode_POST_type(struct HTTP_REQUEST *http_request)
{
    auto it = http_request->headers.find("content-type");
	
	if(it ==  http_request->headers.end())
    {
        return false;
    }
        	                   
	if(it->second == "application/x-www-form-urlencoded")
    {
        http_request->POST_type = HTTP_POST_APPLICATION_X_WWW_FORM_URLENCODED;
    }

    else if(memcmp(it->second.c_str(),"multipart/form-data", 19) == 0)
    {
		http_request->POST_type = HTTP_POST_MULTIPART_FORM_DATA;
    }
    else
    {
        return false;
    }

    return true;
}


bool HTTP_Parse_Request_Headers(const std::string &raw_request, size_t headers_start_offset, std::unordered_map<std::string, std::string> *request_headers, size_t* body_start_offset)
{
    bool use_standard_newline = false;

    size_t start_position = headers_start_offset;
    if(raw_request[headers_start_offset - 2] == '\r')
    {
        use_standard_newline = true;
    }

    size_t stop_position = raw_request.find((use_standard_newline ? "\r\n\r\n" : "\n\n"));
    if (stop_position == std::string::npos)
    {
        return false;
    }

    body_start_offset[0] = stop_position + ((use_standard_newline) ? 4 : 2);

    std::string header_name, header_value;
    while (true)
    {
        if (start_position >= raw_request.size() or raw_request[start_position] == '\n' or raw_request[start_position + 1] == '\n')
        {
            break;
        }

        size_t point_position = raw_request.find(": ", start_position);
        if (point_position == std::string::npos)
        {
            return false;
        }

        header_name = raw_request.substr(start_position, point_position - start_position);
        size_t new_line_position = raw_request.find((use_standard_newline ? "\r\n" : "\n"), point_position);

        if (new_line_position == std::string::npos)
        {
            return false;
        }

        header_value = raw_request.substr(point_position + 2, new_line_position - (point_position + 2));

        //store the value as lowercase
        header_name = str_ansi_to_lower(&header_name);

        //append multiple cookie headers into a single one
        if(header_name == "cookie")
        {
            //append separator in case of multiple cookie headers
            header_value.append(1, ';');
            
            request_headers[0]["cookie"].append(header_value);
        }
        else
        {
             request_headers[0][header_name] = header_value;
        }

        start_position = new_line_position + (use_standard_newline ? 2 : 1);
    }

    return true;
}

int HTTP_Parse_Request_Body(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id, struct HTTP_REQUEST *request, struct HTTP_RESPONSE *response)
{
	if ((request->method == HTTP_METHOD_POST or request->method == HTTP_METHOD_PUT) and request->POST_type == HTTP_POST_TYPE_UNDEFINED)
	{
		if (!HTTP_Decode_POST_type(request))
		{
			return HTTP_Request_Set_Error_Page(worker_id, conn, stream_id, 400);
		}

		unsigned int max_POST_arg_limit = str2uint(&SERVER_CONFIGURATION["max_post_args"]);
		unsigned int max_upload_files_limit = str2uint(&SERVER_CONFIGURATION["max_uploaded_files"]);

		bool continue_if_arg_limit_exceeded = false;
		if (is_server_config_variable_true("continue_if_args_limit_exceeded"))
		{
			continue_if_arg_limit_exceeded = true;
		}

		std::string *request_body = NULL;
		size_t parser_start_offset = 0;

		if (conn->http_version == HTTP_VERSION_2)
		{
			struct HTTP2_CONNECTION *http2_conn = (struct HTTP2_CONNECTION *) conn->raw_connection;
			request_body = &http2_conn->streams[stream_id].recv_buffer;
		}
		else
		{
			struct HTTP1_CONNECTION *http1_conn = (struct HTTP1_CONNECTION *) conn->raw_connection;
			request_body = &http1_conn->recv_buffer;
			parser_start_offset = http1_conn->parser_helper.body_start_offset;
		}

		if (!HTTP_Parse_POST_Body(request, request_body, parser_start_offset, max_POST_arg_limit, max_upload_files_limit, continue_if_arg_limit_exceeded))
		{
			SERVER_LOG_WRITE_ERROR.lock();
			SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING), true);
			SERVER_LOG_WRITE(" The POST request can't be parsed!\n\n", true);
			SERVER_LOG_WRITE_ERROR.unlock();

			HTTP_Request_Set_Error_Page(worker_id, conn, stream_id, 400);

            //to stop processing in request_process function
            return HTTP2_CONNECTION_DELETED;
		}

		request_body->clear();
	}

	return HTTP2_CONNECTION_OK;
}

std::string HTTP_Generate_Set_Cookie_Header(const struct HTTP_COOKIE& cookie) 
{	
    std::string result = cookie.name;
	result.append("=");
	result.append(cookie.value);
	result.append("; ");
	
	if(cookie.expires)
	{
		result.append("Expires: ");
		result.append(convert_ctime2_http_date(cookie.expires));
		result.append("; ");
	}

	if(cookie.max_age)
	{
		result.append("Max-age: ");
		result.append(int2str(cookie.max_age));
		result.append("; ");
	}
	
	if(!cookie.domain.empty())
	{
		result.append("Domain=");
		result.append(cookie.domain);
		result.append("; ");
	}

	if(!cookie.path.empty())
	{
		result.append("Path=");
		result.append(cookie.path);
		result.append("; ");
	}

	if(!cookie.same_site.empty())
	{
		result.append("SameSite=");
		result.append(cookie.same_site);
		result.append("; ");
	}

	if(cookie.http_only)
    {
		result.append("HttpOnly; ");
    }

	if(cookie.secure)
    {
		result.append("Secure; ");
    }

	return result;
}

void HTTP_Parse_Cookie_Header(const std::string& cookie_header, std::unordered_map<std::string,std::string> **cookies)
{
	cookies[0] = new (std::nothrow) std::unordered_map<std::string,std::string>();
	if(!cookies[0])
    {
        SERVER_ERROR_LOG_stdlib_err("Unable to allocate memory for HTTP Cookies!");
        exit(-1);
    }
		
	size_t cookie_header_size = cookie_header.size();
	const char* cookie_buff = cookie_header.c_str();
	
	std::string name,value;
	bool parser_state = 0;
	
	for(size_t i=0; i<cookie_header_size; i++)
	{
		if(!parser_state)
		{
			if(cookie_buff[i] == '=')
            {
				parser_state = 1;
            }
				
			//named cookie without value
			else if(cookie_buff[i] == ';')
			{
				if(!name.empty())
                {
					cookies[0]->insert(std::make_pair(name, ""));
				}

				name.clear();
				
				if(i+1 < cookie_header_size and cookie_buff[i+1] == ' ')
                {
					i++;
                }
			}
			
			else
            {
				name.append(1, cookie_buff[i]);
            }
		}
		else
		{
			if(cookie_buff[i] == ';')
			{
				if(!name.empty())
                {
					cookies[0]->insert(std::make_pair(name, value));
                }
			
				name.clear();
				value.clear();
				
				if(i+1 < cookie_header_size and cookie_buff[i+1] == ' ')
                {
					i++;
                }

				parser_state = 0;
			}
			
			else
            {
				value.append(1, cookie_buff[i]);
            }
		}
	}
	
	if(!name.empty() and !value.empty())
    {
		cookies[0]->insert(std::make_pair(name, value));
    }
}

void HTTP_Init_Cookie(struct HTTP_COOKIE *cookie, const char* name, const char* value)
{
	if(!cookie or !name or !value)
    {
		return;
    }

	cookie->name = name;
	cookie->value = value;
	
	cookie->max_age = 0;
	cookie->expires = (time_t)0;
	
	cookie->http_only = false;
	cookie->secure = false;
}

void HTTP_Init_Cookie(struct HTTP_COOKIE *cookie, const std::string* name, const std::string* value)
{
	if(!cookie or !name or !value)
    {
		return;
    }

	HTTP_Init_Cookie(cookie,name->c_str(),value->c_str());
}

bool HTTP_Parse_Multipart_Form_Data(const std::string *raw_request, size_t start_offset, const std::string *boundary, std::unordered_map<std::string, std::string> *POST_query,
                               std::unordered_map<std::string, std::unordered_map<std::string, struct HTTP_POST_FILE>> *POST_files, unsigned int POST_arg_limit,
                               unsigned int POST_files_limit, bool *POST_arg_limit_exceeded, bool *POST_files_limit_exceeded, bool continue_when_limit_exceeded)
{
    bool use_standard_newline = false;
    if (raw_request->find("\r\n") != std::string::npos)
    {
        use_standard_newline = true;
    }

    size_t POST_body_position = start_offset;

    // boundary separator
    std::string b_separator = "--";
    b_separator.append(*boundary);

    if (use_standard_newline)
    {
        b_separator.append("\r\n");
    }
    else
    {
        b_separator.append("\n");
    }

    b_separator.append("Content-Disposition: form-data; name=\"");
    size_t b_separator_len = b_separator.size();

    std::string document_end = "--";
    document_end.append(*boundary);
    document_end.append("--");

    // content-disposition header
    size_t cd_header_start = raw_request->find(b_separator, POST_body_position);

    if (cd_header_start == std::string::npos)
    {
        b_separator[2 + boundary->size() + (use_standard_newline ? 2 : 1) + 8] = 'd'; // search for "Content-disposition"
        cd_header_start = raw_request->find(b_separator, POST_body_position);

        if (cd_header_start == std::string::npos)
        {
            b_separator[2 + boundary->size() + (use_standard_newline ? 2 : 1)] = 'c'; // search for "content-disposition"
            cd_header_start = raw_request->find(b_separator, POST_body_position);

            if (cd_header_start == std::string::npos)
            {
                return false;
            }
        }
    }

    // prepare limit counters
    // for post args the container size() can be used
    *POST_arg_limit_exceeded = false;
    *POST_files_limit_exceeded = false;
    unsigned int current_file_counter = 0;

    /*
            When all multipart entries are processed,
            the cd_header_start will contain the position of the multipart terminator(document_end)
    */
    while (memcmp(raw_request->c_str() + cd_header_start, document_end.c_str(), document_end.size()) != 0)
    {
        cd_header_start += b_separator_len;

        size_t name_end = raw_request->find('"', cd_header_start);
        if (name_end == std::string::npos)
        {
            return false;
        }

        std::string field_name = raw_request->substr(cd_header_start, name_end - cd_header_start);

        size_t new_line = raw_request->find((use_standard_newline ? "\r\n" : "\n"), name_end);
        if (new_line == std::string::npos)
        {
            return false;
        }

        std::string filename;
        bool is_file = false;

        // check if filename attribute is present
        if (new_line - name_end > 8)
        {
            size_t filename_start = raw_request->find("filename=\"", name_end);
            if (filename_start == std::string::npos or filename_start > new_line) // bad syntax
            {
                return false;
            }

            filename_start += 10; // strlen("filename=\"")

            size_t filename_end = raw_request->find('"', filename_start);
            if (filename_end == std::string::npos or filename_end > new_line) // bad syntax
            {
                return false;
            }

            filename = raw_request->substr(filename_start, filename_end - filename_start);

            is_file = true;
        }

        cd_header_start = raw_request->find(b_separator, cd_header_start);
        if (cd_header_start == std::string::npos)
        {
            cd_header_start = raw_request->rfind(document_end); // try to find the multipart terminator

            if (cd_header_start == std::string::npos) // bad syntax
            {
                return false;
            }
        }

        new_line = raw_request->find((use_standard_newline ? "\r\n" : "\n"), new_line + (use_standard_newline ? 2 : 1));

        size_t multipart_content_start = raw_request->find((use_standard_newline ? "\r\n\r\n" : "\n\n"), name_end);

        if (new_line == std::string::npos or multipart_content_start == std::string::npos or new_line > cd_header_start) // bad syntax
        {
            return false;
        }

        multipart_content_start += (use_standard_newline ? 4 : 2);

        std::string multipart_content = raw_request->substr(multipart_content_start, (cd_header_start - multipart_content_start) - (use_standard_newline ? 2 : 1));

        if (is_file)
        {
            std::string content_type = "text/plain";

            std::string content_type_header = "Content-Type: ";

            auto content_type_i = std::search(raw_request->begin() + name_end, raw_request->begin() + multipart_content_start, content_type_header.begin(), content_type_header.end());

            if (content_type_i == raw_request->begin() + multipart_content_start)
            {
                content_type_header[8] = 't'; // Content-type
                content_type_i = std::search(raw_request->begin() + name_end, raw_request->begin() + multipart_content_start, content_type_header.begin(), content_type_header.end());
            }

            if (content_type_i == raw_request->begin() + multipart_content_start)
            {
                content_type_header[0] = 'c'; // content-type
                content_type_i = std::search(raw_request->begin() + name_end, raw_request->begin() + multipart_content_start, content_type_header.begin(), content_type_header.end());
            }

            if (content_type_i != raw_request->begin() + multipart_content_start) // we have the content type header
            {
                size_t content_type_pos = content_type_i - raw_request->begin() + content_type_header.size();
                new_line = raw_request->find((use_standard_newline ? "\r\n" : "\n"), content_type_pos);

                if (new_line == std::string::npos or new_line > multipart_content_start) // bad syntax
                {
                    return false;
                }

                content_type = raw_request->substr(content_type_pos, new_line - content_type_pos);
            }

            if (!field_name.empty() and !filename.empty() and !(*POST_files_limit_exceeded))
            {

                if (current_file_counter + 1 > POST_files_limit)
                {
                    *POST_files_limit_exceeded = true;
                    if (!continue_when_limit_exceeded)
                    {
                        return false;
                    }
                }
                else
                {
                    POST_files[0][field_name][filename].data = multipart_content;
                    POST_files[0][field_name][filename].type = content_type;

                    current_file_counter++;
                }
            }
        }
        else
        {
            if (!field_name.empty() and !(*POST_arg_limit_exceeded))
            {

                if (POST_query->size() + 1 > POST_arg_limit)
                {
                    *POST_arg_limit_exceeded = true;
                    if (!continue_when_limit_exceeded)
                    {
                        return false;
                    }
                }
                else
                {
                    POST_query[0][field_name] = multipart_content;
                }
            }
        }
    }

    return true;
}

bool HTTP_Parse_POST_Body(struct HTTP_REQUEST *http_request, const std::string* recv_buffer, size_t start_offset, unsigned int args_limit, unsigned int files_limit, bool continue_if_exceeded)
{
    bool args_limit_exceeded = false;
    bool files_limit_exceeded = false;
    bool parse_result;

    if (http_request->POST_type == HTTP_POST_MULTIPART_FORM_DATA)
    {
        http_request->POST_query = new (std::nothrow) std::unordered_map<std::string, std::string>();
        if (!http_request->POST_query)
        {
            SERVER_ERROR_LOG_stdlib_err("Unable to allocate memory for the HTTP_POST_QUERY structure!");
            exit(-1);
        }

        http_request->POST_files = new (std::nothrow) std::unordered_map<std::string, std::unordered_map<std::string, struct HTTP_POST_FILE>>();
        if (!http_request->POST_files)
        {
            SERVER_ERROR_LOG_stdlib_err("Unable to allocate memory for the HTTP_POST_FILES structure!");
            exit(-1);
        }

        auto content_type_iter = http_request->headers.find("content-type");
        if (content_type_iter == http_request->headers.end())
        {
            return false;
        }

        size_t boundary_string_pos = content_type_iter->second.find("boundary=");
        if (boundary_string_pos == std::string::npos)
        {
            return false;
        }

        std::string boundary = content_type_iter->second.substr(boundary_string_pos + 9); // strlen("boundary=")

        parse_result = HTTP_Parse_Multipart_Form_Data(recv_buffer, start_offset, &boundary, http_request->POST_query, http_request->POST_files,
                                                 args_limit, files_limit, &args_limit_exceeded, &files_limit_exceeded, continue_if_exceeded);
    }
    else if (http_request->POST_type == HTTP_POST_APPLICATION_X_WWW_FORM_URLENCODED)
    {
        http_request->POST_query = new (std::nothrow) std::unordered_map<std::string, std::string>();
        if (!http_request->POST_query)
        {
            SERVER_ERROR_LOG_stdlib_err("Unable to allocate memory for the HTTP_POST_QUERY structure!");
            exit(-1);
        }

        parse_result = HTTP_Parse_Query(*recv_buffer, http_request->POST_query, args_limit, &args_limit_exceeded, continue_if_exceeded, start_offset);
    }
    else
    {
        return false;
    }

    if (args_limit_exceeded)
    {
        SERVER_LOG_WRITE_ERROR.lock();
        SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING), true);
        SERVER_LOG_WRITE(" The number of POST arguments exceeded the limit!\n\n", true);
        SERVER_LOG_WRITE_ERROR.unlock();
    }

    if (files_limit_exceeded)
    {
        SERVER_LOG_WRITE_ERROR.lock();
        SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING), true);
        SERVER_LOG_WRITE(" The number of uploaded files exceeded the limit!\n\n", true);
        SERVER_LOG_WRITE_ERROR.unlock();
    }

    return parse_result;
}
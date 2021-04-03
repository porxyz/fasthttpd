#include <vector>
#include <list>
#include <string>
#include <cstring>
#include <thread>
#include <unordered_map>
#include <mutex>
#include <cstdlib>
#include <algorithm>
#include <errno.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "helper_functions.h"
#include "server_journal.h"
#include "https_listener.h"
#include "server_config.h"
#include "file_permissions.h"
#include "custom_bound.h"
#include "http_worker.h"

std::vector<struct http_worker_node> http_workers;
unsigned int next_worker = 0;
std::mutex next_worker_mutex;

std::atomic<size_t> total_http_connections;


bool recv_all(std::list<struct http_connection>::iterator* current_connection,char* buff,size_t buff_size,size_t max_request_limit,bool* limit_exceeded)
{	
	size_t recv_count = current_connection[0]->recv_buffer.size();
	int recv_len;


	sock_read_proc:
	if(current_connection[0]->https)
		recv_len = SSL_read(current_connection[0]->ssl_connection,buff,buff_size);
		
	else
		recv_len = recv(current_connection[0]->client_socket,buff,buff_size,0);

	if(recv_len > 0)
	{
		recv_count += recv_len;
		if(recv_count > max_request_limit)
		{
			limit_exceeded[0] = true;
			return true;
		}
		
		current_connection[0]->recv_buffer.append(buff,recv_len); //std::cout << std::string(buff,recv_len) << std::endl;
		goto sock_read_proc;
	}

	else if(recv_len == 0)
	{
		limit_exceeded[0] = false;
		return true;
	}

	else
	{

		if(current_connection[0]->https)
		{
			int ssl_error = SSL_get_error(current_connection[0]->ssl_connection,recv_len);

			if(ssl_error != SSL_ERROR_WANT_READ)
			{
				SERVER_ERROR_JOURNAL_openssl_err("Unable read from the ssl client socket!");
				return false;
			}

		}

		else
		{
		
			if(errno == EINTR)
				goto sock_read_proc;

			else if(errno != EAGAIN or errno != EWOULDBLOCK)
			{
				SERVER_ERROR_JOURNAL_stdlib_err("Unable read from the client socket!");
				return false;
			}

		}
	}

	limit_exceeded[0] = false;
	return true;
}


bool send_all(std::list<struct http_connection>::iterator* current_connection)
{
	int send_len;

	sock_send_proc:
	char* send_buff = ((char*)current_connection[0]->send_buffer.c_str()) + current_connection[0]->send_buffer_offset;

	if(current_connection[0]->https)
		send_len = SSL_write(current_connection[0]->ssl_connection,send_buff,(current_connection[0]->send_buffer.size() - current_connection[0]->send_buffer_offset));
		
	else
		send_len = send(current_connection[0]->client_socket,send_buff,(current_connection[0]->send_buffer.size() - current_connection[0]->send_buffer_offset),0);


	if(send_len > 0)
	{
		current_connection[0]->send_buffer_offset += send_len;
		goto sock_send_proc;
	}

	else if(send_len == 0)
		return true;

	else
	{

		if(current_connection[0]->https)
		{
			int ssl_error = SSL_get_error(current_connection[0]->ssl_connection,send_len);

			if(ssl_error != SSL_ERROR_WANT_WRITE)
			{
				SERVER_ERROR_JOURNAL_openssl_err("Unable to send through the ssl client socket!");
				return false;
			}

		}


		else
		{
			if(errno == EINTR)
				goto sock_send_proc;

			else if(errno != EAGAIN or errno != EWOULDBLOCK)
			{			
				SERVER_ERROR_JOURNAL_stdlib_err("Unable to send through client socket!");
				return false;
			}

		}
	}

	return true;
}


bool send_from_file(std::list<struct http_connection>::iterator* current_connection,char* buffer,size_t max_read_size)
{
	int read_bytes;
	size_t bytes_to_read,file_len;
	
	attempt_read:
	file_len = current_connection[0]->requested_fd_stop - current_connection[0]->requested_fd_offset;
	bytes_to_read = (file_len > max_read_size) ? max_read_size : file_len;

	if(bytes_to_read == 0)
		return true;

	read_bytes = pread(current_connection[0]->requested_fd,buffer,bytes_to_read,current_connection[0]->requested_fd_offset);

	if(read_bytes == -1)
	{

		if(errno == EINTR)
			goto attempt_read;
	
	
		else
		{
			SERVER_JOURNAL_WRITE_ERROR.lock();
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
			SERVER_JOURNAL_WRITE(" Unable to read from requested file ( ",true);
			SERVER_JOURNAL_WRITE(current_connection[0]->request.URI_path,true);
			SERVER_JOURNAL_WRITE(" )\nError code: 0x",true);
			SERVER_JOURNAL_WRITE(std::hex,true);
			SERVER_JOURNAL_WRITE(errno,true);
			SERVER_JOURNAL_WRITE("\n",true);
			SERVER_JOURNAL_WRITE(strerror(errno),true);
			SERVER_JOURNAL_WRITE("\n\n",true);
			SERVER_JOURNAL_WRITE(std::dec,true);
			SERVER_JOURNAL_WRITE_ERROR.unlock();
			return false;
		}

	}

	current_connection[0]->requested_fd_offset+=read_bytes;

	current_connection[0]->send_buffer_offset = 0;
	current_connection[0]->send_buffer = std::string(buffer,read_bytes);
	
	if(!send_all(current_connection))
		return false;


	if(current_connection[0]->send_buffer.size() == current_connection[0]->send_buffer_offset)
		goto attempt_read;


	return true;
}


int detect_http_version(const std::string* raw_request)
{
	size_t new_line_position = raw_request->find("\r\n");
	if(new_line_position == std::string::npos or new_line_position > 1024 * 5) // 5kb request first line
		return HTTP_VERSION_UNDEFINED;


	size_t last_space_separator = raw_request->find_last_of(' ',new_line_position);
	if(last_space_separator == std::string::npos or (new_line_position - (last_space_separator + 1)) != 8)
		return HTTP_VERSION_UNDEFINED;


	std::string version = std::move(raw_request->substr(last_space_separator+1,8));

	if(version == "HTTP/1.1")
		return HTTP_VERSION_1_1;
		
	if(version == "HTTP/1.0")
		return HTTP_VERSION_1;

	return HTTP_VERSION_UNDEFINED;
}


int detect_http_method(const std::string* raw_request)
{
	size_t new_line_position = raw_request->find("\r\n");
	if(new_line_position == std::string::npos or new_line_position > 1024 * 5) // 5kb request first line
		return HTTP_METHOD_UNDEFINED;


	size_t space_separator = raw_request->find(' ');
	if(space_separator == std::string::npos or space_separator > new_line_position)
		return HTTP_METHOD_UNDEFINED;


	std::string method = std::move(raw_request->substr(0,space_separator));

	if(method == std::string("GET"))
		return HTTP_METHOD_GET;
		
	else if(method == std::string("POST"))
		return HTTP_METHOD_POST;
		
	else if(method == std::string("PUT"))
		return HTTP_METHOD_PUT;
		
	else if(method == std::string("HEAD"))
		return HTTP_METHOD_HEAD;
		
	else if(method == std::string("DELETE"))
		return HTTP_METHOD_DELETE;
		
	else if(method == std::string("OPTIONS"))
		return HTTP_METHOD_OPTIONS;
		
	else if(method == std::string("CONNECT"))
		return HTTP_METHOD_CONNECT;
		
	else if(method == std::string("TRACE"))
		return HTTP_METHOD_TRACE;
		
	else if(method == std::string("PATCH"))
		return HTTP_METHOD_PATCH;

	return HTTP_METHOD_UNDEFINED;
}


bool parse_http_query(const std::string* query_part,std::unordered_map<std::string , std::string>* query_params,unsigned int max_query_args,bool* query_args_limit_exceeded,
		      bool continue_if_exceeded,int start_offset = 0,int end_offset = -1)
{
	if(!query_part or !query_params or !query_args_limit_exceeded)
	    	return false;
    	
    	
    	*query_args_limit_exceeded = false;
    	
	std::string query_name,query_value,dec_query_name,dec_query_value;
	bool parser_state = 0; //0->extracting key ; 1->extracting value
	
	if(end_offset < 0)
		end_offset = query_part->size() - 1;
		
	if(start_offset > end_offset)
		return false;
	
	
	for(int i=start_offset; i<=end_offset; i++)
	{
		if(query_part[0][i] == '=')
			parser_state = 1;
		
		else if(query_part[0][i] == '&')
		{
			parser_state = 0;
			
			if(!url_decode(&query_name,&dec_query_name) or !url_decode(&query_value,&dec_query_value))
				return false;
				
			query_name.clear();
			query_value.clear();
			
			if(!dec_query_name.empty())
				query_params[0][dec_query_name] = dec_query_value;
				
			dec_query_name.clear();
			dec_query_value.clear();	
		}
		
		//extracting value
		else if(parser_state)
			query_value.append(1,query_part[0][i]);
			
		//extracting key	
		else
			query_name.append(1,query_part[0][i]);
			
	}
	
	if(!url_decode(&query_name,&dec_query_name) or !url_decode(&query_value,&dec_query_value))
		return false;
				
	if(!dec_query_name.empty() and !(*query_args_limit_exceeded))
	{
	
		if(query_params->size() + 1 > max_query_args)
		{
			*query_args_limit_exceeded = true;
			
			if(!continue_if_exceeded)
				return false;
		}
		else
			query_params[0][dec_query_name] = dec_query_value;
	}
	
	
    	return true;
}

bool parse_http_URI(const std::string* raw_request,std::string* URI,std::unordered_map<std::string , std::string>* URI_query_params,unsigned int max_arg_limit,bool continue_if_exceeded)
{	
	size_t new_line_position = raw_request->find("\r\n");
	if(new_line_position == std::string::npos or new_line_position > 1024 * 5) // 5kb request first line
		return false;


	size_t first_space_separator = raw_request->find(' ');
	if(first_space_separator == std::string::npos or first_space_separator > new_line_position)
		return false;


	size_t second_space_separator = raw_request->find(' ',first_space_separator+1);
	if(second_space_separator == std::string::npos or second_space_separator > new_line_position)
		return false;


	if((second_space_separator-first_space_separator) - 1 > 1024 * 5) // 5kb request line
		return false; 


	size_t query_mark = raw_request->find('?',first_space_separator);
	if(query_mark == std::string::npos or query_mark > second_space_separator)
	{
		URI[0] = raw_request->substr(first_space_separator+1,(second_space_separator-first_space_separator) - 1);
		return true;
	}

	URI[0] = raw_request->substr(first_space_separator+1,(query_mark-first_space_separator) - 1);

	bool arg_limit_exceeded;
	bool parse_result = parse_http_query(raw_request,URI_query_params,max_arg_limit,&arg_limit_exceeded,continue_if_exceeded,query_mark+1,second_space_separator-1);
	
	if(arg_limit_exceeded)
	{
		SERVER_JOURNAL_WRITE_ERROR.lock();
        	SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
       		SERVER_JOURNAL_WRITE(" The number of query arguments exceeded the limit!\n\n",true);
       		SERVER_JOURNAL_WRITE_ERROR.unlock();
	}
		
	return parse_result;
}



bool parse_http_request_POST_body(std::list<struct http_connection>::iterator* current_connection,unsigned int args_limit,unsigned int files_limit,bool continue_if_exceeded)
{
	bool args_limit_exceeded(0),files_limit_exceeded(0);
	bool parse_result;

	bool use_standard_newline = true;
	
	size_t start_position = current_connection[0]->recv_buffer.find("\r\n\r\n");
	if(start_position == std::string::npos)
	{
		start_position = current_connection[0]->recv_buffer.find("\n\n");
		if(start_position != std::string::npos)
			use_standard_newline = false;
			
		else
			return false;
	}
	

	if(current_connection[0]->request.POST_type == HTTP_POST_MULTIPART_FORM_DATA)
	{
		current_connection[0]->request.POST_query = new (std::nothrow) std::unordered_map<std::string,std::string>();
		if(!current_connection[0]->request.POST_query)
		{
			SERVER_ERROR_JOURNAL_stdlib_err("Unable to allocate memory for the HTTP_POST_QUERY structure!");
        		exit(-1);
		}
		
		current_connection[0]->request.POST_files = new (std::nothrow) std::unordered_map<std::string,std::unordered_map<std::string, struct HTTP_POST_FILE>>();
		if(!current_connection[0]->request.POST_files)
		{
			SERVER_ERROR_JOURNAL_stdlib_err("Unable to allocate memory for the HTTP_POST_FILES structure!");
        		exit(-1);
		}
	
		std::string content_type_h_name = "Content-Type";
	
		auto content_type_iter = current_connection[0]->request.request_headers.find(content_type_h_name);
		if(content_type_iter == current_connection[0]->request.request_headers.end())
		{
			content_type_h_name[8] = 't'; // Content-type
			content_type_iter = current_connection[0]->request.request_headers.find(content_type_h_name);
			
			if(content_type_iter == current_connection[0]->request.request_headers.end())
			{
				content_type_h_name[0] = 'c'; // content-type
				content_type_iter = current_connection[0]->request.request_headers.find(content_type_h_name);
				
				if(content_type_iter == current_connection[0]->request.request_headers.end())
					return false;
			}
			
		}
		
		size_t boundary_string_pos = content_type_iter->second.find("boundary=");
		if(boundary_string_pos == std::string::npos)
			return false;

		std::string boundary = content_type_iter->second.substr(boundary_string_pos + 9); // strlen("boundary=")
		
		parse_result = parse_multipart_form_data(&(current_connection[0]->recv_buffer),&boundary,current_connection[0]->request.POST_query,current_connection[0]->request.POST_files,
							      args_limit,files_limit,&args_limit_exceeded,&files_limit_exceeded,continue_if_exceeded);
		
	}

	else if(current_connection[0]->request.POST_type == HTTP_POST_APPLICATION_X_WWW_FORM_URLENCODED)
	{
		current_connection[0]->request.POST_query = new (std::nothrow) std::unordered_map<std::string,std::string>();
		if(!current_connection[0]->request.POST_query)
		{
			SERVER_ERROR_JOURNAL_stdlib_err("Unable to allocate memory for the HTTP_POST_QUERY structure!");
        		exit(-1);
		}
		
		parse_result = parse_http_query(&(current_connection[0]->recv_buffer),current_connection[0]->request.POST_query,args_limit,
						&args_limit_exceeded,continue_if_exceeded,start_position + (use_standard_newline ? 4 : 2));
	}

	else
		return false;
		
	
		
	if(args_limit_exceeded)
	{
		SERVER_JOURNAL_WRITE_ERROR.lock();
        	SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
       		SERVER_JOURNAL_WRITE(" The number of POST arguments exceeded the limit!\n\n",true);
       		SERVER_JOURNAL_WRITE_ERROR.unlock();
	}
	
		
	if(files_limit_exceeded)
	{
		SERVER_JOURNAL_WRITE_ERROR.lock();
        	SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
        	SERVER_JOURNAL_WRITE(" The number of uploaded files exceeded the limit!\n\n",true);
        	SERVER_JOURNAL_WRITE_ERROR.unlock();
	}
		

	return parse_result;
}


bool parse_http_request_headers(const std::string* raw_request,std::unordered_map<std::string , std::string>* request_headers)
{
	bool use_standard_newline = true;
	
	size_t start_position = raw_request->find("\r\n");
	if(start_position == std::string::npos)
	{
		start_position = raw_request->find("\n");
		
		if(start_position != std::string::npos)
			use_standard_newline = false;
		
		else
			return false;
	}	
	if(start_position == std::string::npos)
		return false;


	start_position += (use_standard_newline ? 2 : 1);

	size_t stop_position = raw_request->find( (use_standard_newline ? "\r\n\r\n" : "\n\n" ) );
	if(stop_position == std::string::npos)
		return false;


	std::string header_name,header_value;
	while(true)
	{
		if(start_position >= raw_request->size() or raw_request[0][start_position] == '\n' or raw_request[0][start_position + 1] == '\n')
			break;


		size_t point_position = raw_request->find(": ",start_position);
		if(point_position == std::string::npos)
			return false;
			
		
		header_name = raw_request->substr(start_position,point_position - start_position);
		size_t new_line_position = raw_request->find( (use_standard_newline ? "\r\n" : "\n" ) ,point_position);
	
		if(new_line_position == std::string::npos)
			return false;
	
		
		header_value = raw_request->substr(point_position + 2,new_line_position - (point_position + 2));

		request_headers[0][header_name] = header_value;

		start_position = new_line_position + (use_standard_newline ? 2 : 1);
	}

	return true;
}


bool parse_multipart_form_data(const std::string* raw_request,const std::string* boundary,std::unordered_map<std::string,std::string> *POST_query,
				std::unordered_map<std::string,std::unordered_map<std::string,struct HTTP_POST_FILE>> *POST_files,unsigned int POST_arg_limit,
				unsigned int POST_files_limit,bool* POST_arg_limit_exceeded,bool* POST_files_limit_exceeded,bool continue_when_limit_exceeded)
{
	if(!raw_request or !boundary or !POST_query or !POST_files or !POST_arg_limit_exceeded or !POST_files_limit_exceeded)
		return false;


	bool use_standard_newline = true;
	size_t POST_body_position = raw_request->find("\r\n\r\n");
	
	if(POST_body_position == std::string::npos)
	{
		 POST_body_position = raw_request->find("\n\n");
		 if(POST_body_position != std::string::npos)
		 	use_standard_newline = false;
		 	
		 else
		 	return false;
	}


	POST_body_position += (use_standard_newline ? 4 : 2);

	//boundary separator
	std::string b_separator = "--";
	b_separator.append(*boundary);
	
	if(use_standard_newline)
		b_separator.append("\r\n");
	else
		b_separator.append("\n");
		
	b_separator.append("Content-Disposition: form-data; name=\"");
	size_t b_separator_len = b_separator.size();
	
	
	std::string document_end = "--";
	document_end.append(*boundary);
	document_end.append("--");
	

	//content-disposition header
	size_t cd_header_start = raw_request->find(b_separator,POST_body_position);

	if(cd_header_start == std::string::npos)
	{
		b_separator[2 + boundary->size() + (use_standard_newline ? 2 : 1) + 8] = 'd'; // search for "Content-disposition"
		cd_header_start = raw_request->find(b_separator,POST_body_position);
		
		if(cd_header_start == std::string::npos)
		{
			b_separator[2 + boundary->size() + (use_standard_newline ? 2 : 1)] = 'c'; // search for "content-disposition"
			cd_header_start = raw_request->find(b_separator,POST_body_position);
			
			if(cd_header_start == std::string::npos)
				return false;
		}
		
	}	
	
	//prepare limit counters
	//for post args the container size() can be used
	*POST_arg_limit_exceeded = false;
	*POST_files_limit_exceeded = false;
	unsigned int current_file_counter = 0;
	
		
	/*
		When all multipart entries are processed, 
		the cd_header_start will contain the position of the multipart terminator(document_end)
	*/
	while(memcmp(raw_request->c_str() + cd_header_start,document_end.c_str(),document_end.size()) != 0)
	{
		cd_header_start += b_separator_len;

		size_t name_end = raw_request->find('"',cd_header_start);
		if(name_end == std::string::npos)
			return false;

	
		std::string field_name = std::move(raw_request->substr(cd_header_start,name_end - cd_header_start));
		
		
		size_t new_line = raw_request->find((use_standard_newline ? "\r\n" : "\n" ),name_end);
		if(new_line == std::string::npos)
			return false;
		
		
		std::string filename;
		bool is_file = false;
		
		//check if filename attribute is present	
		if(new_line - name_end > 8)
		{
			size_t filename_start = raw_request->find("filename=\"",name_end);
			if(filename_start == std::string::npos or filename_start > new_line) //bad syntax
				return false;
			
			filename_start+=10; //strlen("filename=\"")	
			
			size_t filename_end = raw_request->find('"',filename_start);
			if(filename_end == std::string::npos or filename_end > new_line) //bad syntax 
				return false;
			
			filename = std::move(raw_request->substr(filename_start,filename_end-filename_start));
		
			is_file = true;
		}

	
		cd_header_start = raw_request->find(b_separator,cd_header_start);
		if(cd_header_start == std::string::npos)
		{
			cd_header_start = raw_request->rfind(document_end); // try to find the multipart terminator
			
			if(cd_header_start == std::string::npos) // bad syntax
				return false;
		}
	
		new_line = raw_request->find((use_standard_newline ? "\r\n" : "\n" ),new_line + (use_standard_newline ? 2 : 1));
	
		size_t multipart_content_start = raw_request->find((use_standard_newline ? "\r\n\r\n" : "\n\n"),name_end);
	
		if(new_line == std::string::npos or multipart_content_start == std::string::npos or new_line > cd_header_start) // bad syntax
			return false;
	
	
		multipart_content_start += (use_standard_newline ? 4 : 2);
		
		
		std::string multipart_content = std::move(raw_request->substr(multipart_content_start,(cd_header_start - multipart_content_start) - (use_standard_newline ? 2 : 1)) );
		
	
		if(is_file)
		{
			std::string content_type = "text/plain";
			
			std::string content_type_header = "Content-Type: ";
	
			auto content_type_i = std::search(raw_request->begin() + name_end, raw_request->begin() + multipart_content_start,content_type_header.begin(),content_type_header.end());
			
			if(content_type_i == raw_request->begin() + multipart_content_start)
			{
				content_type_header[8] = 't'; // Content-type
				content_type_i = std::search(raw_request->begin() + name_end, raw_request->begin() + multipart_content_start,content_type_header.begin(),content_type_header.end());
			}
			
			if(content_type_i == raw_request->begin() + multipart_content_start)
			{
				content_type_header[0] = 'c'; // content-type
				content_type_i = std::search(raw_request->begin() + name_end, raw_request->begin() + multipart_content_start,content_type_header.begin(),content_type_header.end());
			}
			
			if(content_type_i != raw_request->begin() + multipart_content_start) // we have the content type header
			{
				size_t content_type_pos = content_type_i - raw_request->begin() + content_type_header.size();
				new_line = raw_request->find((use_standard_newline ? "\r\n" : "\n"),content_type_pos);
				
				if(new_line == std::string::npos or new_line > multipart_content_start) // bad syntax
					return false;
				
				content_type = std::move(raw_request->substr(content_type_pos, new_line-content_type_pos));
			}
	
	 		
	 		if(!field_name.empty() and !filename.empty() and !(*POST_files_limit_exceeded))
	 		{
				
				if(current_file_counter + 1> POST_files_limit)
				{
					*POST_files_limit_exceeded = true;
					if(!continue_when_limit_exceeded)
						return false;
				}
				else
				{
					POST_files[0][field_name][filename].data = std::move(multipart_content);
					POST_files[0][field_name][filename].type = std::move(content_type);
				
					current_file_counter++;
				}
			}
		
		}
		else
		{
			if(!field_name.empty() and !(*POST_arg_limit_exceeded))
			{
				
				if(POST_query->size() + 1 > POST_arg_limit)
				{
					*POST_arg_limit_exceeded = true;
					if(!continue_when_limit_exceeded)
						return false;
				}
				else
					POST_query[0][field_name] = std::move(multipart_content);
			}
		}
				
	}
	
		
	return true;
}



//check if request size is smaller than max_size
bool check_request_length(std::list<struct http_connection>::iterator* triggered_connection,size_t worker_id,size_t max_request_len)
{
    std::string content_length_h_name = "Content-Length";
    auto content_length_iter = triggered_connection[0]->request.request_headers.find(content_length_h_name);

    if(content_length_iter == triggered_connection[0]->request.request_headers.end())
    {
        content_length_h_name[8] = 'l';
        content_length_iter = triggered_connection[0]->request.request_headers.find(content_length_h_name);

        if(content_length_iter == triggered_connection[0]->request.request_headers.end())
        {
            content_length_h_name[0] = 'c';
            content_length_iter = triggered_connection[0]->request.request_headers.find(content_length_h_name);

            if(content_length_iter == triggered_connection[0]->request.request_headers.end())
            {
                SERVER_JOURNAL_WRITE_ERROR.lock();
                SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
                SERVER_JOURNAL_WRITE(" The HTTP request is missing the Content-Length header!\n\n",true);
                SERVER_JOURNAL_WRITE_ERROR.unlock();

                set_http_error_page(triggered_connection,411);
                if(!send_all(triggered_connection))
                {
                    delete_http_connection(worker_id,triggered_connection);
                    return false;
                }
                if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size())
                    delete_http_connection(worker_id,triggered_connection);

                return false;
            }

        }
    }

    bool invalid_number;
    size_t content_length_val = str2uint(&content_length_iter->second,&invalid_number);

    if(invalid_number)
    {
        SERVER_JOURNAL_WRITE_ERROR.lock();
        SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
        SERVER_JOURNAL_WRITE(" Content-Length value is not a valid integer!\n\n",true);
        SERVER_JOURNAL_WRITE_ERROR.unlock();

        set_http_error_page(triggered_connection,400);
        if(!send_all(triggered_connection))
        {
            delete_http_connection(worker_id,triggered_connection);
            return false;
        }
        if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size())
            delete_http_connection(worker_id,triggered_connection);

        return false;
    }


    bool use_standard_newline = true;

    size_t body_start_position =  triggered_connection[0]->recv_buffer.find("\r\n\r\n");
    if(body_start_position == std::string::npos)
    {
        body_start_position =  triggered_connection[0]->recv_buffer.find("\n\n");
        if(body_start_position != std::string::npos)
            use_standard_newline = false;

        else
        {
            set_http_error_page(triggered_connection,400);
            if(!send_all(triggered_connection))
            {
                delete_http_connection(worker_id,triggered_connection);
                return false;
            }
            if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size())
                delete_http_connection(worker_id,triggered_connection);

            return false;
        }
    }

    size_t total_size = body_start_position + (use_standard_newline ? 4 : 2) + content_length_val;
    if(total_size > max_request_len)
    {
        SERVER_JOURNAL_WRITE_ERROR.lock();
        SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
        SERVER_JOURNAL_WRITE(" The HTTP request exceed the maximum limit!\n\n",true);
        SERVER_JOURNAL_WRITE_ERROR.unlock();

        set_http_error_page(triggered_connection,413);
        if(!send_all(triggered_connection))
        {
            delete_http_connection(worker_id,triggered_connection);
            return false;
        }
        if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size())
            delete_http_connection(worker_id,triggered_connection);

        return false;
    }

    return true;
}


bool is_POST_request_complete(std::list<struct http_connection>::iterator* triggered_connection)
{
    std::string content_length_h_name = "Content-Length";
    auto content_length_iter = triggered_connection[0]->request.request_headers.find(content_length_h_name);

    if(content_length_iter == triggered_connection[0]->request.request_headers.end())
    {
        content_length_h_name[8] = 'l';
        content_length_iter = triggered_connection[0]->request.request_headers.find(content_length_h_name);

        if(content_length_iter == triggered_connection[0]->request.request_headers.end())
        {
            content_length_h_name[0] = 'c';
            content_length_iter = triggered_connection[0]->request.request_headers.find(content_length_h_name);

            if(content_length_iter == triggered_connection[0]->request.request_headers.end())
                return false;
        }
    }
	
	
    bool is_bad_num;
    size_t post_body_size = str2uint(&content_length_iter->second,&is_bad_num);
	
    if(is_bad_num)
        return false;


    bool use_standard_newline = true;

    size_t body_start_position = triggered_connection[0]->recv_buffer.find("\r\n\r\n");
    if(body_start_position == std::string::npos)
    {
        body_start_position =  triggered_connection[0]->recv_buffer.find("\n\n");
        if(body_start_position != std::string::npos)
            use_standard_newline = false;

        else
            return false;
    }

    body_start_position += (use_standard_newline ? 4 : 2);

    return (triggered_connection[0]->recv_buffer.size() - body_start_position) == post_body_size;
}


bool decode_http_content_range(const std::string* content_range,size_t* offset_start,size_t* offset_stop)
{
	std::vector <std::string> content_range_el;
	explode(content_range,"=",&content_range_el);

	if(content_range_el.size() != 2 or content_range_el[0] != "bytes")
		return false;

	std::vector <std::string> content_range_data;
	explode(&content_range_el[1],"-",&content_range_data);

	if(content_range_data.size() != 2)
		return false;


	bool is_invalid;
	offset_start[0] = str2uint(&content_range_data[0],&is_invalid);

	if(is_invalid)
		return false;


	offset_stop[0] = str2uint(&content_range_data[1],&is_invalid);

	if(is_invalid)
		return false;

	return true;
}

std::string encode_http_content_range(size_t offset_start,size_t offset_stop,size_t file_size)
{
	std::string result = "bytes ";
	result.append(int2str(offset_start)); result.append(1,'-');
	result.append(int2str(offset_stop)); result.append(1,'/');
	result.append(int2str(file_size));
	return result;
}

void generate_http_output(std::list<struct http_connection>::iterator* current_connection)
{
	if(current_connection[0]->http_version == HTTP_VERSION_1)
		current_connection[0]->send_buffer = "HTTP/1.0 ";
		
	else
		current_connection[0]->send_buffer = "HTTP/1.1 ";


	current_connection[0]->send_buffer.append(int2str(current_connection[0]->response.response_code));
	current_connection[0]->send_buffer.append(" ");

	if(current_connection[0]->response.response_code == 100){current_connection[0]->send_buffer.append("Continue");}
	else if(current_connection[0]->response.response_code == 101){current_connection[0]->send_buffer.append("Switching Protocols");}
	else if(current_connection[0]->response.response_code == 102){current_connection[0]->send_buffer.append("Processing");}
	else if(current_connection[0]->response.response_code == 200){current_connection[0]->send_buffer.append("OK");}
	else if(current_connection[0]->response.response_code == 201){current_connection[0]->send_buffer.append("Created");}
	else if(current_connection[0]->response.response_code == 202){current_connection[0]->send_buffer.append("Accepted");}
	else if(current_connection[0]->response.response_code == 203){current_connection[0]->send_buffer.append("Non-Authoritative Information");}
	else if(current_connection[0]->response.response_code == 204){current_connection[0]->send_buffer.append("No Content");}
	else if(current_connection[0]->response.response_code == 205){current_connection[0]->send_buffer.append("Reset Content");}
	else if(current_connection[0]->response.response_code == 206){current_connection[0]->send_buffer.append("Partial Content");}
	else if(current_connection[0]->response.response_code == 300){current_connection[0]->send_buffer.append("Multiple Choices");}
	else if(current_connection[0]->response.response_code == 301){current_connection[0]->send_buffer.append("Moved Permanently");}
	else if(current_connection[0]->response.response_code == 302){current_connection[0]->send_buffer.append("Found");}
	else if(current_connection[0]->response.response_code == 303){current_connection[0]->send_buffer.append("See Other");}
	else if(current_connection[0]->response.response_code == 304){current_connection[0]->send_buffer.append("Not Modified");}
	else if(current_connection[0]->response.response_code == 305){current_connection[0]->send_buffer.append("Use Proxy");}
	else if(current_connection[0]->response.response_code == 306){current_connection[0]->send_buffer.append("Unused");}
	else if(current_connection[0]->response.response_code == 307){current_connection[0]->send_buffer.append("Temporary Redirect");}
	else if(current_connection[0]->response.response_code == 308){current_connection[0]->send_buffer.append("Permanent Redirect");}
	else if(current_connection[0]->response.response_code == 400){current_connection[0]->send_buffer.append("Bad Request");}
	else if(current_connection[0]->response.response_code == 401){current_connection[0]->send_buffer.append("Unauthorized");}
	else if(current_connection[0]->response.response_code == 402){current_connection[0]->send_buffer.append("Payment Required");}
	else if(current_connection[0]->response.response_code == 403){current_connection[0]->send_buffer.append("Forbidden");}
	else if(current_connection[0]->response.response_code == 404){current_connection[0]->send_buffer.append("Not Found");}
	else if(current_connection[0]->response.response_code == 405){current_connection[0]->send_buffer.append("Method Not Allowed");}
	else if(current_connection[0]->response.response_code == 406){current_connection[0]->send_buffer.append("Not Acceptable");}
	else if(current_connection[0]->response.response_code == 407){current_connection[0]->send_buffer.append("Proxy Authentication Required");}
	else if(current_connection[0]->response.response_code == 408){current_connection[0]->send_buffer.append("Request Timeout");}
	else if(current_connection[0]->response.response_code == 409){current_connection[0]->send_buffer.append("Conflict");}
	else if(current_connection[0]->response.response_code == 410){current_connection[0]->send_buffer.append("Gone");}
	else if(current_connection[0]->response.response_code == 411){current_connection[0]->send_buffer.append("Length Required");}
	else if(current_connection[0]->response.response_code == 412){current_connection[0]->send_buffer.append("Precondition Failed");}
	else if(current_connection[0]->response.response_code == 413){current_connection[0]->send_buffer.append("Request Entity Too Large");}
	else if(current_connection[0]->response.response_code == 414){current_connection[0]->send_buffer.append("Request-URI Too Long");}
	else if(current_connection[0]->response.response_code == 415){current_connection[0]->send_buffer.append("Unsupported Media Type");}
	else if(current_connection[0]->response.response_code == 416){current_connection[0]->send_buffer.append("Requested Range Not Satisfiable");}
	else if(current_connection[0]->response.response_code == 417){current_connection[0]->send_buffer.append("Expectation Failed");}
	else if(current_connection[0]->response.response_code == 418){current_connection[0]->send_buffer.append("I'm a teapot");}
	else if(current_connection[0]->response.response_code == 422){current_connection[0]->send_buffer.append("Unprocessable Entity");}
	else if(current_connection[0]->response.response_code == 428){current_connection[0]->send_buffer.append("Precondition Required");}
	else if(current_connection[0]->response.response_code == 429){current_connection[0]->send_buffer.append("Too Many Requests");}
	else if(current_connection[0]->response.response_code == 431){current_connection[0]->send_buffer.append("Request Header Fields Too Large");}
	else if(current_connection[0]->response.response_code == 451){current_connection[0]->send_buffer.append("Unavailable For Legal Reasons");}
	else if(current_connection[0]->response.response_code == 500){current_connection[0]->send_buffer.append("Internal Server Error");}
	else if(current_connection[0]->response.response_code == 501){current_connection[0]->send_buffer.append("Not Implemented");}
	else if(current_connection[0]->response.response_code == 502){current_connection[0]->send_buffer.append("Bad Gateway");}
	else if(current_connection[0]->response.response_code == 503){current_connection[0]->send_buffer.append("Service Unavailable");}
	else if(current_connection[0]->response.response_code == 504){current_connection[0]->send_buffer.append("Gateway Timeout");}
	else if(current_connection[0]->response.response_code == 505){current_connection[0]->send_buffer.append("HTTP Version Not Supported");}
	else if(current_connection[0]->response.response_code == 511){current_connection[0]->send_buffer.append("Network Authentication Required");}
	else if(current_connection[0]->response.response_code == 520){current_connection[0]->send_buffer.append("Web server is returning an unknown error");}
	else if(current_connection[0]->response.response_code == 522){current_connection[0]->send_buffer.append("Connection timed out");}
	else if(current_connection[0]->response.response_code == 524){current_connection[0]->send_buffer.append("A timeout occurred");}
	else{current_connection[0]->send_buffer.append("Undefined");}

	current_connection[0]->send_buffer.append("\r\n");

	if(current_connection[0]->request.request_headers.find("Host") == current_connection[0]->request.request_headers.end())
		current_connection[0]->request.request_headers["Host"] = SERVER_CONFIGURATION["default_host"];


	current_connection[0]->response.response_headers["Host"] = current_connection[0]->request.request_headers["Host"];


	for (auto i = current_connection[0]->response.response_headers.begin(); i != current_connection[0]->response.response_headers.end(); ++i)
	{
		current_connection[0]->send_buffer.append(i->first); current_connection[0]->send_buffer.append(": ");
		current_connection[0]->send_buffer.append(i->second); current_connection[0]->send_buffer.append("\r\n");
	}

	current_connection[0]->send_buffer.append("\r\n");
	current_connection[0]->send_buffer.append(current_connection[0]->response.response_body);

	current_connection[0]->response.response_body.clear();
}

void set_http_error_page(std::list<struct http_connection>::iterator* current_connection,int error_code,std::string reason)
{
	current_connection[0]->response.response_code = error_code;

	if(server_error_page_exists(error_code))
		current_connection[0]->response.response_body = SERVER_ERROR_PAGES[error_code];
		
	else
	{
		reason = "Error ";
		reason.append(int2str(error_code));
		reason.append(" encountered but no error page found!");
		
		current_connection[0]->response.response_code = 500;
		current_connection[0]->response.response_body = SERVER_ERROR_PAGES[500];
	}

	str_replace_first(&current_connection[0]->response.response_body,"$SERVER_NAME",&SERVER_CONFIGURATION["server_name"]);
	str_replace_first(&current_connection[0]->response.response_body,"$SERVER_VERSION",&SERVER_CONFIGURATION["server_version"]);
	str_replace_first(&current_connection[0]->response.response_body,"$OS_NAME",&SERVER_CONFIGURATION["os_name"]);
	str_replace_first(&current_connection[0]->response.response_body,"$OS_VERSION",&SERVER_CONFIGURATION["os_version"]);
	str_replace_first(&current_connection[0]->response.response_body,"$SERVER_PORT",int2str(current_connection[0]->server_port).c_str());
	str_replace_first(&current_connection[0]->response.response_body,"$REASON",&reason);


	std::string path_url;
	if(current_connection[0]->request.URI_path.size() > 128)
	{
		path_url = current_connection[0]->request.URI_path.substr(0,128);
		path_url.append("...");
	}
	
	else
		path_url = current_connection[0]->request.URI_path;


	str_replace_first(&current_connection[0]->response.response_body,"$URL",html_special_chars_escape(&path_url).c_str());

	if(current_connection[0]->request.request_headers.find("Host") == current_connection[0]->request.request_headers.end())
		str_replace_first(&current_connection[0]->response.response_body,"$HOSTNAME",&SERVER_CONFIGURATION["default_host"]);

	else
		str_replace_first(&current_connection[0]->response.response_body,"$HOSTNAME",&current_connection[0]->request.request_headers["Host"]);


	if(current_connection[0]->https)
	{
		std::string ssl_info = "("; ssl_info.append(OPENSSL_VERSION_TEXT);
		ssl_info.append(")");
		str_replace_first(&current_connection[0]->response.response_body,"$SSL_INFO",&ssl_info);
	}
	else
		str_replace_first(&current_connection[0]->response.response_body,"$SSL_INFO","");


	current_connection[0]->response.response_headers["Content-Type"] = "text/html; charset=utf-8";
	current_connection[0]->request.request_headers["Accept-Ranges"] = "none";
	current_connection[0]->response.response_headers["Content-Length"] = int2str(current_connection[0]->response.response_body.size());

	auto last_modified_header_p = current_connection[0]->response.response_headers.find("Last-Modified");
	if(last_modified_header_p != current_connection[0]->response.response_headers.end())
		current_connection[0]->response.response_headers.erase(last_modified_header_p);


	generate_http_output(current_connection);
	current_connection[0]->state = HTTP_STATE_CONTENT_BOUND;
}


void generate_http_folder_response(const std::string* full_path,std::list<struct http_connection>::iterator* current_connection)
{
	struct dirent* dir_entry;
	DIR* folder = opendir(full_path->c_str());

	if(folder == NULL)
	{
		set_http_error_page(current_connection,500);
		return;
	}

	current_connection[0]->response.response_code = 200;
	current_connection[0]->response.response_headers["Content-Type"] = "text/html; charset=utf-8";
	current_connection[0]->request.request_headers["Accept-Ranges"] = "none";

	current_connection[0]->response.response_body = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Directory listening</title></head><body>"
							"<p style=\"position:relative; font-size:125%; left:1%; width:98%; border-bottom:2px solid gray;\">Directory listening of <span style=\"font-style:italic;\">";

	if(current_connection[0]->request.URI_path.size() > 128)
	{
		std::string short_url = current_connection[0]->request.URI_path.substr(0,128);
		short_url.append("...");
		current_connection[0]->response.response_body.append(html_special_chars_escape(&short_url));
	}
	else
		current_connection[0]->response.response_body.append(html_special_chars_escape(&current_connection[0]->request.URI_path));


	current_connection[0]->response.response_body.append("</span></p><br>");

	std::string file_link;

	size_t num_links = 0;
	while ((dir_entry = readdir(folder)) != NULL)
	{

		if(num_links == 128)
		{
			current_connection[0]->response.response_body.append("<p>...</p>");
			break;
		}


		if(strcmp(".",dir_entry->d_name) == 0 or strcmp(".access_config",dir_entry->d_name) == 0)
			continue;

		file_link = current_connection[0]->request.URI_path;  

		if(current_connection[0]->request.URI_path[current_connection[0]->request.URI_path.size() - 1] != '/')
			file_link.append(1,'/');
			
			
		file_link.append(dir_entry->d_name);

		file_link = rectify_path(&file_link);

		current_connection[0]->response.response_body.append("<p><a href=\"");
		current_connection[0]->response.response_body.append(file_link);
		current_connection[0]->response.response_body.append("\">");

		if(strlen(dir_entry->d_name) < 128)
			current_connection[0]->response.response_body.append(html_special_chars_escape(dir_entry->d_name));

		else
		{
			std::string trimmed_d_name = dir_entry->d_name; trimmed_d_name.append("...");
			current_connection[0]->response.response_body.append(html_special_chars_escape(&trimmed_d_name));
		}

		current_connection[0]->response.response_body.append("</a></p>");
		num_links+=1;
	}

	closedir(folder);


	current_connection[0]->response.response_body.append("<p style=\"position:absolute; left:1%; width:98%; border-top:2px solid gray; bottom:0; font-style: italic;\">");
	current_connection[0]->response.response_body.append(SERVER_CONFIGURATION["server_name"]);
	current_connection[0]->response.response_body.append("/");
	current_connection[0]->response.response_body.append(SERVER_CONFIGURATION["server_version"]);
	current_connection[0]->response.response_body.append(" (");
	current_connection[0]->response.response_body.append(SERVER_CONFIGURATION["os_name"]);
	current_connection[0]->response.response_body.append("/");
	current_connection[0]->response.response_body.append(SERVER_CONFIGURATION["os_version"]);
	current_connection[0]->response.response_body.append(") on ");

	if(current_connection[0]->request.request_headers.find("Host") == current_connection[0]->request.request_headers.end())
		current_connection[0]->response.response_body.append(SERVER_CONFIGURATION["default_host"]);

	else
		current_connection[0]->response.response_body.append(current_connection[0]->request.request_headers["Host"]);


	current_connection[0]->response.response_body.append(" port ");
	current_connection[0]->response.response_body.append(int2str(current_connection[0]->server_port).c_str());

	if(current_connection[0]->https)
	{
		current_connection[0]->response.response_body.append(" (");
		current_connection[0]->response.response_body.append(OPENSSL_VERSION_TEXT);
		current_connection[0]->response.response_body.append(")");
	}	

	current_connection[0]->response.response_body.append("</body></html>");
	current_connection[0]->response.response_headers["Content-Length"] = int2str(current_connection[0]->response.response_body.size());


	if(current_connection[0]->request.request_method == HTTP_METHOD_HEAD)
		current_connection[0]->response.response_body.clear();

	generate_http_output(current_connection);
	current_connection[0]->state = HTTP_STATE_CONTENT_BOUND;
}

void worker_add_client(int new_client,const char* remote_addr,std::string* server_addr,int16_t remote_port,int16_t server_port,int ip_addr_version,bool https,SSL_CTX* openssl_ctx)
{
	int worker_id;
	
	next_worker_mutex.lock();
	
	worker_id = next_worker;
	next_worker++;
	
	if(next_worker >= http_workers.size())
		next_worker = 0;
		
	next_worker_mutex.unlock();


	struct http_connection current_connection;

	current_connection.client_socket = new_client;
	current_connection.remote_addr = remote_addr;
	current_connection.server_addr = server_addr[0];
	current_connection.remote_port = remote_port;
	current_connection.server_port = server_port;
	current_connection.ip_addr_version = ip_addr_version;
	current_connection.milisecond_timeout = str2uint(&SERVER_CONFIGURATION["request_timeout"]) * 1000;
	current_connection.https = https;
	current_connection.http_version = HTTP_VERSION_UNDEFINED;
	current_connection.send_buffer_offset = 0;
	current_connection.state = https ? HTTP_STATE_SSL_INIT : HTTP_STATE_INIT;
	
	current_connection.request.request_method = HTTP_METHOD_UNDEFINED;
	current_connection.request.POST_type = HTTP_POST_TYPE_UNDEFINED;
	current_connection.request.POST_query = NULL;
	current_connection.request.POST_files = NULL;
	
	current_connection.response.response_headers["Server"] = SERVER_CONFIGURATION["server_name"];
	current_connection.response.response_headers["Server"].append(1,'/');
	current_connection.response.response_headers["Server"].append(SERVER_CONFIGURATION["server_version"]);
	current_connection.response.response_headers["Date"] = convert_ctime2_http_date(time(NULL));
	current_connection.response.response_headers["Connection"] = "close";

	if(https)
	{
		current_connection.ssl_connection = SSL_new(openssl_ctx);
		if(current_connection.ssl_connection == NULL)
		{
			SERVER_ERROR_JOURNAL_openssl_err("Unable to create SSL!");
			exit(-1);
		}

		SSL_set_fd(current_connection.ssl_connection,new_client);
	}


	if(clock_gettime(CLOCK_MONOTONIC,&current_connection.last_action) == -1)
	{
		SERVER_ERROR_JOURNAL_stdlib_err("Unable to get time!");
		exit(-1);
	}

	http_workers[worker_id].connections_mutex->lock();
	http_workers[worker_id].connections.push_back(current_connection);

	std::list<struct http_connection>::iterator last_insertion = http_workers[worker_id].connections.end();
	last_insertion--;

	last_insertion->self_reference = last_insertion;

	struct epoll_event epoll_config;
	epoll_config.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
	epoll_config.data.ptr = (void*)&last_insertion->self_reference;

	if(epoll_ctl(http_workers[worker_id].worker_epoll,EPOLL_CTL_ADD,new_client,&epoll_config) == -1)
	{
		SERVER_ERROR_JOURNAL_stdlib_err("Unable to add the client to the worker epoll!");
		exit(-1);
	}

	http_workers[worker_id].connections_mutex->unlock();
	
}


void free_http_connection(std::list<struct http_connection>::iterator* current_connection)
{
	current_connection[0]->recv_buffer.clear();
	current_connection[0]->send_buffer.clear();
	current_connection[0]->send_buffer_offset = 0;

	current_connection[0]->response.response_headers.clear();
	current_connection[0]->response.response_body.clear();

	current_connection[0]->request.request_headers.clear();
	current_connection[0]->request.URI_path.clear();
	current_connection[0]->request.URI_query.clear();
	
	
	if(current_connection[0]->request.POST_query)
		delete(current_connection[0]->request.POST_query);
	
	if(current_connection[0]->request.POST_files)
		delete(current_connection[0]->request.POST_files);

	if(current_connection[0]->state == HTTP_STATE_FILE_BOUND and current_connection[0]->requested_fd != -1)
	{
		close(current_connection[0]->requested_fd);
		current_connection[0]->requested_fd = -1;
	}

}

void delete_http_connection(size_t worker_id,std::list<struct http_connection>::iterator* current_connection,bool allow_keepalive,bool lock_mutex)
{
	if(allow_keepalive and (current_connection[0]->state != HTTP_STATE_INIT or current_connection[0]->state != HTTP_STATE_SSL_INIT))
	{
	        auto keepalive_iter = current_connection[0]->request.request_headers.find("Connection");
		    
		if(keepalive_iter == current_connection[0]->request.request_headers.end())
	       		keepalive_iter = current_connection[0]->request.request_headers.find("connection");
		  
		    
		if(keepalive_iter != current_connection[0]->request.request_headers.end() and keepalive_iter->second == "keep-alive")
		{		
			free_http_connection(current_connection);
			current_connection[0]->milisecond_timeout = str2uint(&SERVER_CONFIGURATION["request_timeout"]) * 1000;

			current_connection[0]->response.response_headers["Server"] = SERVER_CONFIGURATION["server_name"];
			current_connection[0]->response.response_headers["Server"].append(1,'/');
			current_connection[0]->response.response_headers["Server"].append(SERVER_CONFIGURATION["server_version"]);
			current_connection[0]->response.response_headers["Date"] = convert_ctime2_http_date(time(NULL));
			current_connection[0]->response.response_headers["Connection"] = "close";

			
			if(current_connection[0]->request.POST_query)
			{
				delete(current_connection[0]->request.POST_query);
				current_connection[0]->request.POST_query = NULL;
			}
	
			if(current_connection[0]->request.POST_files)
			{
				delete(current_connection[0]->request.POST_files);
				current_connection[0]->request.POST_files = NULL;
			}
		
			current_connection[0]->request.POST_type = HTTP_POST_TYPE_UNDEFINED;
					
			current_connection[0]->http_version = HTTP_VERSION_UNDEFINED;
			current_connection[0]->request.request_method = HTTP_METHOD_UNDEFINED;
					

			current_connection[0]->state = HTTP_STATE_INIT;
			return;
		}

	}

	free_http_connection(current_connection);

	close(current_connection[0]->client_socket);

	if(current_connection[0]->https)
		SSL_free(current_connection[0]->ssl_connection);


	if(lock_mutex)
		http_workers[worker_id].connections_mutex->lock();
			
	http_workers[worker_id].connections.erase(current_connection[0]);
	
	if(lock_mutex)
		http_workers[worker_id].connections_mutex->unlock();


	
	total_http_connections--;
}



void http_worker_thread(int worker_id)
{
    init_worker_aux_modules(worker_id);
    
    struct epoll_event triggered_event;

    size_t max_request_size = str2uint(&SERVER_CONFIGURATION["max_request_size"]) * 1024;
    
    unsigned int max_query_arg_limit = str2uint(&SERVER_CONFIGURATION["max_query_args"]);
    unsigned int max_POST_arg_limit = str2uint(&SERVER_CONFIGURATION["max_post_args"]);
    unsigned int max_upload_files_limit = str2uint(&SERVER_CONFIGURATION["max_uploaded_files"]);
    
    bool continue_if_arg_limit_exceeded = false;
    if(server_config_variable_exists("continue_if_args_limit_exceeded") && (SERVER_CONFIGURATION["continue_if_args_limit_exceeded"] == "1" or SERVER_CONFIGURATION["continue_if_args_limit_exceeded"] == "true"))
    	continue_if_arg_limit_exceeded = true;
    

    size_t recv_buffer_size = str2uint(&SERVER_CONFIGURATION["read_buffer_size"]) * 1024;
    char* recv_buffer = new (std::nothrow) char[recv_buffer_size];
    if(recv_buffer == 0)
    {
        SERVER_ERROR_JOURNAL_stdlib_err("Unable to allocate memory for the http socket buffer!");
        exit(-1);
    }

    int epoll_wait_time = 500;
    while(true)
    {
        int epoll_result = epoll_wait(http_workers[worker_id].worker_epoll,&triggered_event,1,epoll_wait_time);

        if(epoll_result == -1)
        {

            if(errno != EINTR)
            {
                SERVER_ERROR_JOURNAL_stdlib_err("Unable to listen to epoll!");
                exit(-1);
            }

            continue;
        }


        struct timespec current_time;
        if(clock_gettime(CLOCK_MONOTONIC,&current_time) == -1)
        {
            SERVER_ERROR_JOURNAL_stdlib_err("Unable to get time!");
            exit(-1);
        }

        if(epoll_result > 0 and triggered_event.data.ptr == 0)
            break;

        http_workers[worker_id].connections_mutex->lock();

        for(auto i=http_workers[worker_id].connections.begin(); i != http_workers[worker_id].connections.end(); ++i)
        {
            int64_t elapsed_miliseconds = ((current_time.tv_sec * 1000) + (current_time.tv_nsec / 1000000)) - ((i->last_action.tv_sec * 1000) + (i->last_action.tv_nsec / 1000000));

            if(i->milisecond_timeout == 0)
                continue;

            if(elapsed_miliseconds > i->milisecond_timeout)
            {
                if(epoll_result > 0 and i == *((std::list<struct http_connection>::iterator*)triggered_event.data.ptr))
                    continue;

                std::list<struct http_connection>::iterator expired_connection = i;
                i++;

                delete_http_connection(worker_id,&expired_connection,false,false);
                continue;
            }

        }


        http_workers[worker_id].connections_mutex->unlock();


        if(epoll_result > 0)
        {
            std::list<struct http_connection>::iterator* triggered_connection = (std::list<struct http_connection>::iterator*)triggered_event.data.ptr;
            triggered_connection[0]->last_action = current_time;

            if (triggered_event.events & EPOLLHUP or triggered_event.events & EPOLLRDHUP or triggered_event.events & EPOLLERR)
            {
                delete_http_connection(worker_id,triggered_connection);
                continue;
            }

            else if(triggered_event.events & EPOLLIN)
            {

                if(triggered_connection[0]->state == HTTP_STATE_INIT)
                {
                    bool max_request_limit_exceeded;
                    if(!recv_all(triggered_connection,recv_buffer,recv_buffer_size,max_request_size,&max_request_limit_exceeded))
                    {
                        delete_http_connection(worker_id,triggered_connection);
                        continue;
                    }

                    if(max_request_limit_exceeded)
                    {
                        SERVER_JOURNAL_WRITE_ERROR.lock();
                        SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
                        SERVER_JOURNAL_WRITE(" The HTTP request exceed the maximum limit!\n\n",true);
                        SERVER_JOURNAL_WRITE_ERROR.unlock();

                        set_http_error_page(triggered_connection,413);
                        if(!send_all(triggered_connection))
                        {
                            delete_http_connection(worker_id,triggered_connection);
                            continue;
                        }
                        if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size())
                            delete_http_connection(worker_id,triggered_connection);

                        continue;
                    }

                    if(triggered_connection[0]->http_version == HTTP_VERSION_UNDEFINED)
                    {
                        int hv = detect_http_version(&triggered_connection[0]->recv_buffer); // detect http version
                        if(hv == HTTP_VERSION_UNDEFINED)
                        {
                            SERVER_JOURNAL_WRITE_ERROR.lock();
                            SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
                            SERVER_JOURNAL_WRITE(" The request HTTP version is undefined!\n\n",true);
                            SERVER_JOURNAL_WRITE_ERROR.unlock();
                        
                            set_http_error_page(triggered_connection,400);
                            if(!send_all(triggered_connection))
                            {
                                delete_http_connection(worker_id,triggered_connection);
                                continue;
                            }
                            if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size())
                                delete_http_connection(worker_id,triggered_connection);

                            continue;
                        }

                        triggered_connection[0]->http_version = hv;
                    }


                    // detect http method
                    if(triggered_connection[0]->request.request_method == HTTP_METHOD_UNDEFINED)
                    {
                        triggered_connection[0]->request.request_method = detect_http_method(&triggered_connection[0]->recv_buffer);

                        if(triggered_connection[0]->request.request_method == HTTP_METHOD_UNDEFINED)
                        {
                            SERVER_JOURNAL_WRITE_ERROR.lock();
                            SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
                            SERVER_JOURNAL_WRITE(" The HTTP request method is not defined!\n\n",true);
                            SERVER_JOURNAL_WRITE_ERROR.unlock();

                            set_http_error_page(triggered_connection,400);
                            if(!send_all(triggered_connection))
                            {
                                delete_http_connection(worker_id,triggered_connection);
                                continue;
                            }
                            if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size())
                                delete_http_connection(worker_id,triggered_connection);

                            continue;
                        }
                    }


                    // decode url path and query args
                    if(!parse_http_URI(&triggered_connection[0]->recv_buffer,&triggered_connection[0]->request.URI_path,&triggered_connection[0]->request.URI_query,max_query_arg_limit,continue_if_arg_limit_exceeded))
                    {
                    	SERVER_JOURNAL_WRITE_ERROR.lock();
                        SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
                        SERVER_JOURNAL_WRITE(" The request URI can't be parsed!\n\n",true);
                        SERVER_JOURNAL_WRITE_ERROR.unlock();
                            
                        set_http_error_page(triggered_connection,400);
                        if(!send_all(triggered_connection))
                        {
                            delete_http_connection(worker_id,triggered_connection);
                            continue;
                        }
                        if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size())
                            delete_http_connection(worker_id,triggered_connection);

                        continue;
                    }

                    if(triggered_connection[0]->recv_buffer.find("\r\n\r\n") != std::string::npos) // request not fully transmitted
                    {

                        if(!parse_http_request_headers(&triggered_connection[0]->recv_buffer,&triggered_connection[0]->request.request_headers)) // bad headers
                        {
                        	SERVER_JOURNAL_WRITE_ERROR.lock();
                        	SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
                        	SERVER_JOURNAL_WRITE(" The request headers can't be parsed!\n\n",true);
                        	SERVER_JOURNAL_WRITE_ERROR.unlock();
                        
                        	set_http_error_page(triggered_connection,400);
                                if(!send_all(triggered_connection))
                                {
                                	delete_http_connection(worker_id,triggered_connection);
                                	continue;
                            	}

                           	if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size())
                                	delete_http_connection(worker_id,triggered_connection);

                            	continue;
                        }

                    }

                    else
                        continue;


                    // default host; http 1.0 support
                    if(triggered_connection[0]->request.request_headers.find("Host") == triggered_connection[0]->request.request_headers.end())
                    {
                    	SERVER_JOURNAL_WRITE_ERROR.lock();
                        SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
                        SERVER_JOURNAL_WRITE(" The request doesn't have a host header!\n\n",true);
                        SERVER_JOURNAL_WRITE_ERROR.unlock();
                        
                        triggered_connection[0]->request.request_headers["Host"] = SERVER_CONFIGURATION["default_host"];
                    }

                    if(SERVER_HOSTNAMES.find(triggered_connection[0]->request.request_headers["Host"]) == SERVER_HOSTNAMES.end()) // bad host
                    {
                    	SERVER_JOURNAL_WRITE_ERROR.lock();
                        SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
                        SERVER_JOURNAL_WRITE(" The requested host is not defined in the hosts list!\n\n",true);
                        SERVER_JOURNAL_WRITE_ERROR.unlock();
                        
                        set_http_error_page(triggered_connection,400);
                        if(!send_all(triggered_connection))
                        {
                            delete_http_connection(worker_id,triggered_connection);
                            continue;
                        }
                        if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size())
                            delete_http_connection(worker_id,triggered_connection);

                        continue;
                    }

                    // keep alive
                    if(triggered_connection[0]->request.request_headers.find("Connection") != triggered_connection[0]->request.request_headers.end() and triggered_connection[0]->request.request_headers["Connection"] == "keep-alive")
                            triggered_connection[0]->response.response_headers["Connection"] = "keep-alive";



                    /*
                     Check if the request has the declared size bigger that maximum request
                     If the request has no content-length header then return error 411
                     Only for POST requests
                    */
                    if(triggered_connection[0]->request.request_method == HTTP_METHOD_POST and !check_request_length(triggered_connection,worker_id,max_request_size))
                        continue;



                    std::string full_path = SERVER_HOSTNAMES[triggered_connection[0]->request.request_headers["Host"]];
                    full_path.append(1,'/');
                    full_path.append(triggered_connection[0]->request.URI_path);

                    int check_file_code = check_file_access(&full_path,&SERVER_HOSTNAMES[triggered_connection[0]->request.request_headers["Host"]]);
                    if(check_file_code != 0)
                    {
                        set_http_error_page(triggered_connection,check_file_code);
                        if(!send_all(triggered_connection))
                        {
                            delete_http_connection(worker_id,triggered_connection);
                            continue;
                        }
                        if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size())
                            delete_http_connection(worker_id,triggered_connection,true);

                        continue;
                    }


                    struct custom_bound_entry custom_page_generator;
                    if(check_custom_bound_path(&full_path,&SERVER_HOSTNAMES[triggered_connection[0]->request.request_headers["Host"]],&custom_page_generator))
                    {
                    
                    	if(triggered_connection[0]->request.request_method == HTTP_METHOD_POST && triggered_connection[0]->request.POST_type == HTTP_POST_TYPE_UNDEFINED && is_POST_request_complete(triggered_connection))
                    	{ 
                    		if(triggered_connection[0]->request.request_headers.find("Content-Type") != triggered_connection[0]->request.request_headers.end())
				{

					if(triggered_connection[0]->request.request_headers["Content-Type"] == std::string("application/x-www-form-urlencoded"))
						triggered_connection[0]->request.POST_type = HTTP_POST_APPLICATION_X_WWW_FORM_URLENCODED;

					else if(memcmp(triggered_connection[0]->request.request_headers["Content-Type"].c_str(),"multipart/form-data",19) == 0)
						triggered_connection[0]->request.POST_type = HTTP_POST_MULTIPART_FORM_DATA;

				}

				if(!parse_http_request_POST_body(triggered_connection,max_POST_arg_limit,max_upload_files_limit,continue_if_arg_limit_exceeded))
				{
					SERVER_JOURNAL_WRITE_ERROR.lock();
                        		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
                        		SERVER_JOURNAL_WRITE(" The POST request can't be parsed!\n\n",true);
                        		SERVER_JOURNAL_WRITE_ERROR.unlock();
                        
					set_http_error_page(triggered_connection,400);
					if(!send_all(triggered_connection))
					{
						delete_http_connection(worker_id,triggered_connection);
						continue;
					}

					if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size())
						delete_http_connection(worker_id,triggered_connection,true);

					continue;
				}
                    	}
                    
                        triggered_connection[0]->state = HTTP_STATE_CUSTOM_BOUND;
                        triggered_connection[0]->response.response_code = 200;
                        triggered_connection[0]->response.response_headers["Content-Type"] = "text/html; charset=utf-8";
                        run_custom_page_generator(triggered_connection,worker_id,&custom_page_generator);
                        continue;
                    }


                    if(triggered_connection[0]->request.request_method != HTTP_METHOD_GET and triggered_connection[0]->request.request_method != HTTP_METHOD_HEAD)
                    {
                    	SERVER_JOURNAL_WRITE_ERROR.lock();
                        SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
                        SERVER_JOURNAL_WRITE(" The requested method is not supported on this resource!\n\n",true);
                        SERVER_JOURNAL_WRITE_ERROR.unlock();
                        
                        set_http_error_page(triggered_connection,405);
                        if(!send_all(triggered_connection))
                        {
                            delete_http_connection(worker_id,triggered_connection);
                            continue;
                        }
                        if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size())
                            delete_http_connection(worker_id,triggered_connection,true);

                        continue;
                    }

                    full_path = rectify_path(&full_path);

                    uint64_t requested_file_size; time_t requested_file_mdate; bool is_folder;
                    if(get_file_info(&full_path,&requested_file_size,&requested_file_mdate,&is_folder) == -1) // get file size and modification date
                    {
                        set_http_error_page(triggered_connection,404);
                        if(!send_all(triggered_connection))
                        {
                            delete_http_connection(worker_id,triggered_connection);
                            continue;
                        }
                        if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size())
                            delete_http_connection(worker_id,triggered_connection,true);

                        continue;
                    }


                    if(is_folder)
                    {
                        generate_http_folder_response(&full_path,triggered_connection);
                        if(!send_all(triggered_connection))
                        {
                            delete_http_connection(worker_id,triggered_connection);
                            continue;
                        }
                        if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size())
                            delete_http_connection(worker_id,triggered_connection,true);

                        continue;
                    }

                    triggered_connection[0]->response.response_headers["Last-Modified"] = convert_ctime2_http_date(requested_file_mdate);

                    triggered_connection[0]->response.response_headers["Content-Type"] = get_MIME_type_by_ext(&full_path);
                    triggered_connection[0]->response.response_headers["Accept-Ranges"] = "bytes";

                    if(triggered_connection[0]->request.request_headers.find("If-Modified-Since") != triggered_connection[0]->request.request_headers.end())
                    {

                        time_t mod_time;
                        if(!convert_http_date2_ctime(&triggered_connection[0]->request.request_headers["If-Modified-Since"],&mod_time))
                        {
                            SERVER_JOURNAL_WRITE_ERROR.lock();
                            SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
                            SERVER_JOURNAL_WRITE(" The request If-Modified-Since header can't be parsed!\n\n",true);
                            SERVER_JOURNAL_WRITE_ERROR.unlock();
                        
                            set_http_error_page(triggered_connection,400);
                            if(!send_all(triggered_connection))
                            {
                                delete_http_connection(worker_id,triggered_connection);
                                continue;
                            }
                            if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size())
                                delete_http_connection(worker_id,triggered_connection,true);

                            continue;
                        }

                        if(requested_file_mdate <= mod_time)
                        {
                            triggered_connection[0]->response.response_code = 304;
                            generate_http_output(triggered_connection);
                            triggered_connection[0]->state = HTTP_STATE_CONTENT_BOUND;
                            if(!send_all(triggered_connection))
                            {
                                delete_http_connection(worker_id,triggered_connection);
                                continue;
                            }
                            if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size())
                                delete_http_connection(worker_id,triggered_connection,true);

                            continue;
                        }

                    }

                    // support partial request
                    if(triggered_connection[0]->request.request_headers.find("Range") != triggered_connection[0]->request.request_headers.end())
                    {
                        size_t req_start,req_stop;
                        if(!decode_http_content_range(&triggered_connection[0]->request.request_headers["Range"],&req_start,&req_stop))
                        {
                            SERVER_JOURNAL_WRITE_ERROR.lock();
                            SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
                            SERVER_JOURNAL_WRITE(" The request Range header can't be parsed!\n\n",true);
                            SERVER_JOURNAL_WRITE_ERROR.unlock();
                            
                            set_http_error_page(triggered_connection,416);
                            if(!send_all(triggered_connection))
                            {
                                delete_http_connection(worker_id,triggered_connection);
                                continue;
                            }
                            if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size())
                                delete_http_connection(worker_id,triggered_connection,true);

                            continue;
                        }

                        if(req_start >= req_stop or req_stop > requested_file_size)
                        {
                            SERVER_JOURNAL_WRITE_ERROR.lock();
                            SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
                            SERVER_JOURNAL_WRITE(" The request Range header is not valid!\n\n",true);
                            SERVER_JOURNAL_WRITE_ERROR.unlock();
                            
                            set_http_error_page(triggered_connection,416);
                            if(!send_all(triggered_connection))
                            {
                                delete_http_connection(worker_id,triggered_connection);
                                continue;
                            }
                            if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size())
                                delete_http_connection(worker_id,triggered_connection,true);

                            continue;
                        }

                        triggered_connection[0]->response.response_headers["Content-Range"] = encode_http_content_range(req_start,req_stop,requested_file_size);
                        triggered_connection[0]->response.response_code = 206;
                        triggered_connection[0]->response.response_headers["Content-Length"] = int2str(req_stop - req_start);
                        triggered_connection[0]->requested_fd_offset = req_start;
                        triggered_connection[0]->requested_fd_stop = req_stop;
                    }

                    else
                    {
                        triggered_connection[0]->response.response_code = 200;
                        triggered_connection[0]->response.response_headers["Content-Length"] = int2str(requested_file_size);
                        triggered_connection[0]->requested_fd_offset = 0;
                        triggered_connection[0]->requested_fd_stop = requested_file_size;
                    }


                    if(triggered_connection[0]->request.request_method == HTTP_METHOD_HEAD)
                    {
                        generate_http_output(triggered_connection);
                        if(!send_all(triggered_connection))
                        {
                            delete_http_connection(worker_id,triggered_connection);
                            continue;
                        }
                        if(triggered_connection[0]->send_buffer_offset != triggered_connection[0]->send_buffer.size())
                        {
                            delete_http_connection(worker_id,triggered_connection);
                            continue;
                        }
                        continue;
                    }

                    triggered_connection[0]->requested_fd = open(full_path.c_str(),O_RDONLY | O_CLOEXEC);
                    if(triggered_connection[0]->requested_fd == -1) // to be modified
                    {
                    	SERVER_JOURNAL_WRITE_ERROR.lock();
                        SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
                        SERVER_JOURNAL_WRITE(" The server is unable to open the following resource!\n",true);
                        SERVER_JOURNAL_WRITE("Path: ",true);
                       // SERVER_JOURNAL_WRITE("Path: ",true);
                        SERVER_JOURNAL_WRITE("\n\n",true);
                        SERVER_JOURNAL_WRITE_ERROR.unlock();
                            
                        set_http_error_page(triggered_connection,500);
                        if(!send_all(triggered_connection))
                        {
                            delete_http_connection(worker_id,triggered_connection);
                            continue;
                        }
                        if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size())
                            delete_http_connection(worker_id,triggered_connection,true);

                        continue;
                    }


                    generate_http_output(triggered_connection); // send headers only
                    triggered_connection[0]->state = HTTP_STATE_FILE_BOUND;
                    if(!send_all(triggered_connection))
                    {
                        delete_http_connection(worker_id,triggered_connection);
                        continue;
                    }
                    if(triggered_connection[0]->send_buffer_offset != triggered_connection[0]->send_buffer.size())
                    {
                        delete_http_connection(worker_id,triggered_connection);
                        continue;
                    }


                    if(!send_from_file(triggered_connection,recv_buffer,recv_buffer_size))
                    {
                        delete_http_connection(worker_id,triggered_connection);
                        continue;
                    }

                    if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size() and
                            triggered_connection[0]->requested_fd_offset == triggered_connection[0]->requested_fd_stop)
                        delete_http_connection(worker_id,triggered_connection,true);
                }


                else if(triggered_connection[0]->state == HTTP_STATE_SSL_INIT)
                {
                    int ssl_status = SSL_accept(triggered_connection[0]->ssl_connection);
                    if (ssl_status  <= 0)
                    {

                        int ssl_errno = SSL_get_error(triggered_connection[0]->ssl_connection,ssl_status);

                        if(ssl_errno == SSL_ERROR_WANT_READ or ssl_errno == SSL_ERROR_WANT_WRITE)
                        	continue;


                        SERVER_ERROR_JOURNAL_openssl_err("Unable to accept the incoming SSL connection!");

                        delete_http_connection(worker_id,triggered_connection);
                        continue;
                    }

                    triggered_connection[0]->state = HTTP_STATE_INIT;
                }


            }



            else if(triggered_event.events & EPOLLOUT)
            {

                if(triggered_connection[0]->state == HTTP_STATE_CONTENT_BOUND)
                {

                    if(triggered_connection[0]->send_buffer_offset != triggered_connection[0]->send_buffer.size())
                    {
                        if(!send_all(triggered_connection))
                        {
                            delete_http_connection(worker_id,triggered_connection);
                            continue;
                        }
                    }

                    if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size())
                    {
                        delete_http_connection(worker_id,triggered_connection,true);
                        continue;
                    }

                }



                else if(triggered_connection[0]->state == HTTP_STATE_FILE_BOUND)
                {

                    if(triggered_connection[0]->send_buffer_offset != triggered_connection[0]->send_buffer.size())
                    {
                        if(!send_all(triggered_connection))
                        {
                            delete_http_connection(worker_id,triggered_connection);
                            continue;
                        }
                    }

                    if(!send_from_file(triggered_connection,recv_buffer,recv_buffer_size))
                    {
                        delete_http_connection(worker_id,triggered_connection);
                        continue;
                    }

                    if(triggered_connection[0]->send_buffer_offset == triggered_connection[0]->send_buffer.size() and
                            triggered_connection[0]->requested_fd_offset == triggered_connection[0]->requested_fd_stop)
                    {
                        delete_http_connection(worker_id,triggered_connection,true);
                        continue;
                    }

                }


                else if (triggered_connection[0]->state == HTTP_STATE_SSL_INIT)
                {
                    int ssl_status = SSL_accept(triggered_connection[0]->ssl_connection);
                    if (ssl_status  <= 0)
                    {

                        int ssl_errno = SSL_get_error(triggered_connection[0]->ssl_connection,ssl_status);

                        if(ssl_errno == SSL_ERROR_WANT_READ or ssl_errno == SSL_ERROR_WANT_WRITE)
                        	continue;


                        SERVER_ERROR_JOURNAL_openssl_err("Unable to accept the incoming SSL connection!");

                        delete_http_connection(worker_id,triggered_connection);
                        continue;
                    }

                    triggered_connection[0]->state = HTTP_STATE_INIT;
                }

            }



            else
            {
                SERVER_JOURNAL_WRITE_ERROR.lock();
                SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
                SERVER_JOURNAL_WRITE(" Unknown event occured on http worker epoll!\nEvent: 0x",true);
                SERVER_JOURNAL_WRITE(std::hex,true);
                SERVER_JOURNAL_WRITE(triggered_event.events,true);
                SERVER_JOURNAL_WRITE("\n\n",true);
                SERVER_JOURNAL_WRITE(std::dec,true);
                SERVER_JOURNAL_WRITE_ERROR.unlock();
            }


        }



    }

    delete[] recv_buffer;
    delete(http_workers[worker_id].connections_mutex);
    close(http_workers[worker_id].worker_epoll);
    

    for(std::list<struct http_connection>::iterator i = http_workers[worker_id].connections.begin(); i != http_workers[worker_id].connections.end(); ++i)
    {
        std::list<struct http_connection>::iterator conn = i;
        i++;
        delete_http_connection(worker_id,&conn,false,false);
    }
    
    
    free_worker_aux_modules(worker_id);
}

void init_workers(int close_trigger)
{
	for(unsigned int i=0; i<str2uint(&SERVER_CONFIGURATION["server_workers"]); i++)
	{
		struct http_worker_node this_worker;

		this_worker.worker_epoll = epoll_create1(EPOLL_CLOEXEC);
		if(this_worker.worker_epoll == -1)
		{
			SERVER_ERROR_JOURNAL_stdlib_err("Unable to create the http worker epoll!");
			exit(-1);
		}

		struct epoll_event epoll_config;
		epoll_config.events = EPOLLIN | EPOLLET;
		epoll_config.data.ptr = 0;

		if(epoll_ctl(this_worker.worker_epoll,EPOLL_CTL_ADD,close_trigger,&epoll_config) == -1)
		{
			SERVER_ERROR_JOURNAL_stdlib_err("Unable to add the server close trigger to epoll!");
			exit(-1);
		}


		http_workers.push_back(this_worker);
		http_workers[http_workers.size() - 1].worker_thread = new std::thread(http_worker_thread,http_workers.size() -1);
		http_workers[http_workers.size() - 1].connections_mutex = new std::mutex();
		
	}
}


void wait_for_workers_to_exit()
{
	for(unsigned int i=0; i<http_workers.size(); i++)
	{
		http_workers[i].worker_thread->join();
		delete(http_workers[i].worker_thread);
	}
	
}

void init_worker_aux_modules(int worker_id)
{
	#ifndef NO_MOD_MYSQL
	http_workers[worker_id].mysql_db_handle = new (std::nothrow) mysql_connection();
	if(http_workers[worker_id].mysql_db_handle == NULL)
	{
        	SERVER_ERROR_JOURNAL_stdlib_err("MOD_MYSQL: Unable to allocate memory for the mysql connection!");
		exit(-1);
	}


	if(is_server_config_variable_true("enable_MOD_MYSQL"))
		init_MYSQL_connection(http_workers[worker_id].mysql_db_handle);

	#endif
}


void free_worker_aux_modules(int worker_id)
{
	#ifndef NO_MOD_MYSQL
	delete(http_workers[worker_id].mysql_db_handle);
	#endif
}









#include "http2_frame_processor.h"
#include "http_worker.h"
#include "http_parser.h"
#include "hpack_api.h"

#include "../server_log.h"
#include "../server_config.h"
#include "../helper_functions.h"
#include "../endianness_conversions.h"

int HTTP2_Frame_Settings_Process(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn)
{
	struct HTTP2_CONNECTION *http2_conn = (struct HTTP2_CONNECTION *) conn->raw_connection;

	// check if frame is valid
	if (http2_conn->recv_frame_header.length > 36 or http2_conn->recv_frame_header.stream_id != 0)
	{
		return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_PROTOCOL_ERROR, 0, "invalid settings frame");
	}

	if (http2_conn->recv_frame_header.length == 0 and !(http2_conn->recv_frame_header.flags & HTTP2_FRAME_FLAG_ACK))
	{
		return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_PROTOCOL_ERROR, 0, "invalid settings frame");
	}

	struct HTTP2_SETTINGS_PARAMETER *settings_param;

	for (unsigned int i = 0; i < http2_conn->recv_frame_header.length; i += sizeof(struct HTTP2_SETTINGS_PARAMETER))
	{
		settings_param = (struct HTTP2_SETTINGS_PARAMETER*) (http2_conn->recv_buffer.c_str() + i);

		// convert to host endianness
		uint16_t parameter_id = endian_conv_ntoh16(settings_param->id);

		if (parameter_id == HTTP2_SETTINGS_HPACK_TABLE_SIZE)
		{
			http2_conn->client_settings.hpack_table_size = endian_conv_ntoh32(settings_param->value);
		}
		else if (parameter_id == HTTP2_SETTINGS_ENABLE_PUSH)
		{
			http2_conn->client_settings.enable_push = endian_conv_ntoh32(settings_param->value);
		}
		else if (parameter_id == HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS)
		{
			http2_conn->client_settings.max_concurrent_streams = endian_conv_ntoh32(settings_param->value);
		}
		else if (parameter_id == HTTP2_SETTINGS_INITIAL_WINDOW_SIZE)
		{
			http2_conn->client_settings.init_window_size = endian_conv_ntoh32(settings_param->value);
			
			if(http2_conn->client_settings.init_window_size < HTTP2_SETTINGS_INITIAL_WINDOW_SIZE_MIN or http2_conn->client_settings.init_window_size > HTTP2_SETTINGS_INITIAL_WINDOW_SIZE_MAX)
			{
				return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_PROTOCOL_ERROR, 0, "invalid settings parameter (init window size)");
			}
		}
		else if (parameter_id == HTTP2_SETTINGS_MAX_FRAME_SIZE)
		{
			http2_conn->client_settings.max_frame_size = endian_conv_ntoh32(settings_param->value);

			if(http2_conn->client_settings.max_frame_size < HTTP2_SETTINGS_MAX_FRAME_SIZE_MIN or http2_conn->client_settings.max_frame_size > HTTP2_SETTINGS_MAX_FRAME_SIZE_MAX)
			{
				return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_PROTOCOL_ERROR, 0, "invalid settings parameter (max frame size)");
			}
		}
		else if (parameter_id == HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE)
		{
			http2_conn->client_settings.max_header_list_size = endian_conv_ntoh32(settings_param->value);
		}
		else
		{
			return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_PROTOCOL_ERROR, 0, "invalid settings parameter (unknown parameter)");
		}
	}

	HTTP2_Connection_Insert_Frame(http2_conn, sizeof(struct HTTP2_FRAME_HEADER), (uint8_t *)HTTP2_Frame_Settings_ACK);

	if (conn->state == HTTP2_CONNECTION_STATE_WAIT_SETTINGS)
	{
		conn->state = HTTP2_CONNECTION_STATE_NORMAL;
	}

	return HTTP2_CONNECTION_OK;
}

void HTTP2_Frame_Settings_Generate(struct HTTP2_CONNECTION* http2_conn)
{
	struct HTTP2_FRAME_CONTAINER frame_container;
	frame_container.length = sizeof(struct HTTP2_FRAME_HEADER) + (sizeof(struct HTTP2_SETTINGS_PARAMETER) * 6);	
	
	frame_container.contents = new (std::nothrow) uint8_t[frame_container.length];

	if (!frame_container.contents)
	{
		SERVER_ERROR_LOG_stdlib_err("Unable to allocate memory for a HTTP2 frame.");
		exit(-1);
	}

	struct HTTP2_FRAME_HEADER* frame_header = (struct HTTP2_FRAME_HEADER*) frame_container.contents;
	frame_header->stream_id = 0;
	frame_header->type = HTTP2_FRAME_TYPE_SETTINGS;
	frame_header->flags = 0;
	frame_header->length = endian_conv_hton24(sizeof(struct HTTP2_SETTINGS_PARAMETER) * 6);

	struct HTTP2_SETTINGS_PARAMETER* parameters = (struct HTTP2_SETTINGS_PARAMETER*) (frame_container.contents + sizeof(struct HTTP2_FRAME_HEADER));

	parameters[0].id = endian_conv_hton16(HTTP2_SETTINGS_HPACK_TABLE_SIZE);
	parameters[0].value = endian_conv_hton32(http2_conn->server_settings.hpack_table_size);

	parameters[1].id = endian_conv_hton16(HTTP2_SETTINGS_ENABLE_PUSH);
	parameters[1].value = endian_conv_hton32(http2_conn->server_settings.enable_push);

	parameters[2].id = endian_conv_hton16(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS);
	parameters[2].value = endian_conv_hton32(http2_conn->server_settings.max_concurrent_streams);

	parameters[3].id = endian_conv_hton16(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE);
	parameters[3].value = endian_conv_hton32(http2_conn->server_settings.init_window_size);

	parameters[4].id = endian_conv_hton16(HTTP2_SETTINGS_MAX_FRAME_SIZE);
	parameters[4].value = endian_conv_hton32(http2_conn->server_settings.max_frame_size);

	parameters[5].id = endian_conv_hton16(HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE);
	parameters[5].value = endian_conv_hton32(http2_conn->server_settings.max_header_list_size);

	http2_conn->frame_queue.push_front(frame_container);
}

int HTTP2_Frame_Window_Update_Process(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn)
{
	struct HTTP2_CONNECTION *http2_conn = (struct HTTP2_CONNECTION *) conn->raw_connection;

	if (http2_conn->recv_frame_header.length != 4)
	{
		return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_PROTOCOL_ERROR, 0, "invalid window_update frame");
	}

	uint32_t increment = endian_conv_ntoh_http31(((uint32_t *)http2_conn->recv_buffer.c_str())[0]);

	if (increment == 0)
	{
		return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_PROTOCOL_ERROR, 0, "invalid window_update increment value");
	}
	
	if (http2_conn->recv_frame_header.stream_id == 0)
	{
		http2_conn->send_window_avail_bytes += increment;

		if (http2_conn->send_window_avail_bytes > 0xFFFFFFFFLL)
		{	
			return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_FLOW_CONTROL_ERROR, 0, "control flow window is too big");
		}
	}
	else // update the send window on a stream
	{
		uint32_t stream_id = http2_conn->recv_frame_header.stream_id;
		// check if the stream is in use
		if (http2_conn->streams.find(stream_id) == http2_conn->streams.end())
		{
			if(HTTP2_Stream_Init(worker_id, conn, stream_id) == HTTP2_CONNECTION_DELETED)
			{
				return HTTP2_CONNECTION_DELETED;
			}
		}

		http2_conn->streams[stream_id].send_window_avail_bytes += increment;

		if (http2_conn->streams[stream_id].send_window_avail_bytes > 0xFFFFFFFFLL)
		{
			return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_FLOW_CONTROL_ERROR, stream_id, "control flow window is too big");
		}

		if(http2_conn->streams[stream_id].state == HTTP2_STREAM_STATE_FILE_BOUND)
		{
			if(HTTP2_Stream_Send_From_File(worker_id, conn, stream_id) == HTTP2_CONNECTION_DELETED)
			{
				return HTTP2_CONNECTION_DELETED;
			}
		}
	}

	return HTTP2_Connection_Send_Enqueued_Frames(worker_id, conn);
}

int HTTP2_Frame_Window_Update_Generate(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id, const uint32_t increment)
{
	struct HTTP2_CONNECTION *http2_conn = (struct HTTP2_CONNECTION *) conn->raw_connection;
	
	struct HTTP2_FRAME_CONTAINER frame_container;
	frame_container.length = sizeof(struct HTTP2_FRAME_HEADER) + 4;	
	
	frame_container.contents = new (std::nothrow) uint8_t[frame_container.length];

	if (!frame_container.contents)
	{
		SERVER_ERROR_LOG_stdlib_err("Unable to allocate memory for a HTTP2 frame.");
		exit(-1);
	}

	struct HTTP2_FRAME_HEADER* frame_header = (struct HTTP2_FRAME_HEADER*) frame_container.contents;
	frame_header->stream_id = endian_conv_ntoh32(stream_id);
	frame_header->type = HTTP2_FRAME_TYPE_WINDOW_UPDATE;
	frame_header->flags = 0;
	frame_header->length = endian_conv_hton24(4);

	uint32_t* window_increment = (uint32_t*)  (frame_container.contents + sizeof(struct HTTP2_FRAME_HEADER));
	*window_increment = endian_conv_hton32(increment);

	if(stream_id == 0)
	{
		http2_conn->frame_queue.push_front(frame_container);
	}
	else
	{
		http2_conn->frame_queue.push_back(frame_container);
	}

	return HTTP2_Connection_Send_Enqueued_Frames(worker_id, conn);
}

int HTTP2_Frame_Header_Process(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn)
{
	struct HTTP2_CONNECTION *http2_conn = (struct HTTP2_CONNECTION *)conn->raw_connection;

	uint32_t stream_id = http2_conn->recv_frame_header.stream_id;
	if (stream_id == 0)
	{
		return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_PROTOCOL_ERROR, 0, "headers frame on the stream 0");
	}

	uint8_t *header_block_start = (uint8_t *)http2_conn->recv_buffer.c_str();
	int32_t header_block_size = (int32_t)http2_conn->recv_frame_header.length;

	bool is_padded = http2_conn->recv_frame_header.flags & HTTP2_FRAME_FLAG_PADDED;
	if (is_padded)
	{
		uint8_t pad_len = header_block_start[0];
		header_block_start++;
		header_block_size -= pad_len + 1;
	}

	bool is_last_header_block = http2_conn->recv_frame_header.flags & HTTP2_FRAME_FLAG_END_HEADERS;
	bool is_last_block = http2_conn->recv_frame_header.flags & HTTP2_FRAME_FLAG_END_STREAM;

	// if the block is the last block, it must be the last header block
	if (is_last_block and !is_last_header_block)
	{
		return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_PROTOCOL_ERROR, stream_id, "end_stream but not end_headers present");
	}

	// deprecated, but useful when retrieving the raw data
	bool has_priority_info = http2_conn->recv_frame_header.flags & HTTP2_FRAME_FLAG_PRIORITY;
	if (has_priority_info)
	{
		// skip 5 octets
		header_block_start += 5;
		header_block_size -= 5;
	}

	// validate the block size
	if (header_block_size <= 0)
	{
		return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_FRAME_SIZE_ERROR, stream_id, "frame size error (<= 0)");
	}

	// create a new stream
	if (http2_conn->streams.find(stream_id) == http2_conn->streams.end())
	{
		if(HTTP2_Stream_Init(worker_id, conn, stream_id) == HTTP2_CONNECTION_DELETED)
		{
			return HTTP2_CONNECTION_DELETED;
		}
	}
	else if (http2_conn->streams[stream_id].state != HTTP2_STREAM_STATE_WAIT_HEADERS)
	{
		return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_PROTOCOL_ERROR, stream_id, "did not expect a headers frame for this stream");
	}
	
	struct HTTP2_STREAM& current_stream = http2_conn->streams[stream_id];
	current_stream.recv_buffer.append((const char *)header_block_start, header_block_size);

	uint64_t max_req_size = 0;
	uint64_t deflated_headers_size = 0;

	if (is_last_header_block)
	{
		// parse the header
		std::unordered_map<std::string, std::string> *decoded_headers = &current_stream.request.headers;
		if (!HPACK_decode_headers(http2_conn->hpack_decoder, current_stream.recv_buffer, decoded_headers))
		{
			return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_COMPRESSION_ERROR, stream_id);
		}

		for(auto header_it = current_stream.request.headers.begin(); header_it != current_stream.request.headers.end(); header_it++)
		{
			deflated_headers_size += header_it->first.size();
			deflated_headers_size += header_it->second.size();
		}

		// extract the special headers to look like a normal request
		auto h2_header_path = decoded_headers->find(":path");
		auto h2_header_method = decoded_headers->find(":method");
		auto h2_header_authority = decoded_headers->find(":authority");

		if (h2_header_path != decoded_headers->end())
		{
			unsigned int max_query_arg_limit = str2uint(&SERVER_CONFIGURATION["max_query_args"]);

			bool continue_if_arg_limit_exceeded = false;
			if (is_server_config_variable_true("continue_if_args_limit_exceeded"))
			{
				continue_if_arg_limit_exceeded = true;
			}

			if(!HTTP_Parse_Raw_URI(h2_header_path->second, &current_stream.request.URI_path, &current_stream.request.URI_query, max_query_arg_limit, continue_if_arg_limit_exceeded))
			{
				return HTTP_Request_Set_Error_Page(worker_id, conn, stream_id, 400);
			}
		}

		if (h2_header_method != decoded_headers->end())
		{
			current_stream.request.method = HTTP_Parse_Method(h2_header_method->second);
		}

		if (h2_header_authority != decoded_headers->end())
		{
			current_stream.request.headers["host"] = h2_header_authority->second;
		}

		max_req_size = str2uint(SERVER_CONFIGURATION["max_request_size"]) * 1024;
		if(deflated_headers_size > max_req_size)
		{
			return HTTP_Request_Set_Error_Page(worker_id, conn, stream_id, 413);
		}

		current_stream.recv_buffer.clear();
		current_stream.state = HTTP2_STREAM_STATE_WAIT_BODY;
	}

	if (is_last_block)
	{
		current_stream.state = HTTP2_STREAM_STATE_PROCESSING;

		// process the request
		if(HTTP_Request_Process(worker_id, conn, stream_id) == HTTP2_CONNECTION_DELETED)
		{
			return HTTP2_CONNECTION_DELETED;
		}
	}
	else
	{
		auto content_len_h = current_stream.request.headers.find("content-length");
		if (content_len_h == current_stream.request.headers.end())
		{
			return HTTP2_Stream_Reset(worker_id, conn, stream_id, HTTP2_ERROR_CODE_PROTOCOL_ERROR);
		}

		bool invalid_number;
		current_stream.expected_request_body_size = str2uint(content_len_h->second, &invalid_number);

		if(invalid_number)
		{
			return HTTP2_Stream_Reset(worker_id, conn, stream_id, HTTP2_ERROR_CODE_PROTOCOL_ERROR);
		}
		else if(current_stream.expected_request_body_size + deflated_headers_size > max_req_size)
		{
			return HTTP2_Stream_Reset(worker_id, conn, stream_id, HTTP2_ERROR_CODE_REFUSED_STREAM);
		}

	}

	return HTTP2_CONNECTION_OK;
}

int HTTP2_Frame_Data_Process(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn)
{
	struct HTTP2_CONNECTION *http2_conn = (struct HTTP2_CONNECTION *)conn->raw_connection;

	uint32_t stream_id = http2_conn->recv_frame_header.stream_id;
	if (stream_id == 0)
	{
		return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_PROTOCOL_ERROR, 0, "data frame on the stream 0");
	}

	uint8_t *data_block_start = (uint8_t *)http2_conn->recv_buffer.c_str();
	int32_t data_block_size = (int32_t)http2_conn->recv_frame_header.length;

	bool is_padded = http2_conn->recv_frame_header.flags & HTTP2_FRAME_FLAG_PADDED;
	if (is_padded)
	{
		uint8_t pad_len = data_block_start[0];
		data_block_start++;
		data_block_size -= pad_len + 1;
	}

	bool is_last_block = http2_conn->recv_frame_header.flags & HTTP2_FRAME_FLAG_END_STREAM;

	// deprecated, but useful when retrieving the raw data
	bool has_priority_info = http2_conn->recv_frame_header.flags & HTTP2_FRAME_FLAG_PRIORITY;
	if (has_priority_info)
	{
		// skip 5 octets
		data_block_start += 5;
		data_block_size -= 5;
	}

	// validate the block size
	if (data_block_size <= 0)
	{
		return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_FRAME_SIZE_ERROR, stream_id, "frame size error (<= 0)");
	}

	if (http2_conn->streams.find(stream_id) == http2_conn->streams.end())
	{
		return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_PROTOCOL_ERROR, stream_id, "data frame on a closed stream");
	}
	else if (http2_conn->streams[stream_id].state != HTTP2_STREAM_STATE_WAIT_BODY)
	{
		return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_PROTOCOL_ERROR, stream_id, "did not expect a data frame for this stream");
	}

	auto& current_stream = http2_conn->streams[stream_id];

	//update flow control window
	current_stream.recv_window_avail_bytes -= http2_conn->recv_frame_header.length;
	if(current_stream.recv_window_avail_bytes <= 0)
	{
		return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_FLOW_CONTROL_ERROR, stream_id, "data frame is bigger than control window");
	}
	else if(current_stream.recv_window_avail_bytes <= http2_conn->server_settings.init_window_size/2)
	{	
		uint32_t window_increment = http2_conn->server_settings.init_window_size - current_stream.recv_window_avail_bytes;
		current_stream.recv_window_avail_bytes += window_increment;

		if(HTTP2_Frame_Window_Update_Generate(worker_id, conn, stream_id, window_increment) == HTTP2_CONNECTION_DELETED)
		{
			return HTTP2_CONNECTION_DELETED;
		}
	}

	//acutal len is bigger than the content-len
	if((current_stream.recv_buffer.size() + data_block_size) > current_stream.expected_request_body_size)
	{
		return HTTP2_Stream_Reset(worker_id, conn, stream_id, HTTP2_ERROR_CODE_PROTOCOL_ERROR);
	}

	current_stream.recv_buffer.append((const char *)data_block_start, data_block_size);

	if (is_last_block)
	{
		//content-len is different from the actual len
		if (current_stream.expected_request_body_size != current_stream.recv_buffer.size())
		{
			return HTTP2_Stream_Reset(worker_id, conn, stream_id, HTTP2_ERROR_CODE_PROTOCOL_ERROR);
		}

		current_stream.state = HTTP2_STREAM_STATE_PROCESSING;

		// process the request
		return HTTP_Request_Process(worker_id, conn, stream_id);
	}

	return HTTP2_CONNECTION_OK;
}

int HTTP2_Frame_Reset_Stream_Process(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn)
{	
	struct HTTP2_CONNECTION* http2_conn = (struct HTTP2_CONNECTION*) conn->raw_connection;
	const uint32_t stream_id = http2_conn->recv_frame_header.stream_id;

	if(http2_conn->streams.find(stream_id) == http2_conn->streams.end())
	{
		return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_PROTOCOL_ERROR, stream_id, "rst_stream on a closed stream");
	}

	HTTP2_Stream_Delete(worker_id, http2_conn, stream_id);

	return HTTP2_CONNECTION_OK;
}

int HTTP2_Frame_Ping_Process(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn)
{	
	struct HTTP2_CONNECTION* http2_conn = (struct HTTP2_CONNECTION*) conn->raw_connection;

	if(http2_conn->recv_frame_header.stream_id != 0)
	{
		return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_PROTOCOL_ERROR, 0, "invalid ping frame");
	}

	if(http2_conn->recv_frame_header.length != 8)
	{
		return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_FRAME_SIZE_ERROR, 0, "invalid ping frame");
	}

	if(!(http2_conn->recv_frame_header.flags & HTTP2_FRAME_FLAG_ACK))
	{
		//send a ping frame with ACK flag
		struct HTTP2_FRAME_CONTAINER frame_container;
		frame_container.length = sizeof(struct HTTP2_FRAME_HEADER) + 8;	
	
		frame_container.contents = new (std::nothrow) uint8_t[frame_container.length];

		if (!frame_container.contents)
		{
			SERVER_ERROR_LOG_stdlib_err("Unable to allocate memory for a HTTP2 frame.");
			exit(-1);
		}

		struct HTTP2_FRAME_HEADER* frame_header = (struct HTTP2_FRAME_HEADER*) frame_container.contents;
		frame_header->stream_id = 0;
		frame_header->type = HTTP2_FRAME_TYPE_PING;
		frame_header->flags = HTTP2_FRAME_FLAG_ACK;
		frame_header->length = endian_conv_hton24(8);

		uint64_t* ping_frame_contents = (uint64_t*)(frame_container.contents + sizeof(struct HTTP2_FRAME_HEADER));
		ping_frame_contents[0] = *((uint64_t*)http2_conn->recv_buffer.c_str());

		//a ping frame must be sent with high priority
		http2_conn->frame_queue.push_front(frame_container);
	
		return HTTP2_Connection_Send_Enqueued_Frames(worker_id, conn);
	}

	return HTTP2_CONNECTION_OK;
}

int HTTP2_Frame_Process(int worker_id, struct GENERIC_HTTP_CONNECTION *conn)
{
	struct HTTP2_CONNECTION *http2_conn = (struct HTTP2_CONNECTION *) conn->raw_connection;

	if (conn->state == HTTP2_CONNECTION_STATE_WAIT_SETTINGS)
	{
		if (http2_conn->recv_frame_header.type == HTTP2_FRAME_TYPE_SETTINGS and http2_conn->recv_frame_header.flags == 0)
		{
			return HTTP2_Frame_Settings_Process(worker_id, conn);
		}
		else
		{
			return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_PROTOCOL_ERROR, 0, "expected settings frame");
		}
	}
	else
	{
		if (http2_conn->recv_frame_header.type == HTTP2_FRAME_TYPE_SETTINGS)
		{
			return HTTP2_Frame_Settings_Process(worker_id, conn);
		}
		else if (http2_conn->recv_frame_header.type == HTTP2_FRAME_TYPE_WINDOW_UPDATE)
		{
			return HTTP2_Frame_Window_Update_Process(worker_id, conn);
		}
		else if (http2_conn->recv_frame_header.type == HTTP2_FRAME_TYPE_HEADERS or http2_conn->recv_frame_header.type == HTTP2_FRAME_TYPE_CONTINUATION)
		{
			return HTTP2_Frame_Header_Process(worker_id, conn);
		}
		else if (http2_conn->recv_frame_header.type == HTTP2_FRAME_TYPE_DATA)
		{
			return HTTP2_Frame_Data_Process(worker_id, conn);
		}
		else if(http2_conn->recv_frame_header.type == HTTP2_FRAME_TYPE_GOAWAY)
		{
			return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_NO_ERROR, 0);
		}
		else if(http2_conn->recv_frame_header.type == HTTP2_FRAME_TYPE_RESET_STREAM)
		{
			return HTTP2_Frame_Reset_Stream_Process(worker_id, conn);
		}
		else if(http2_conn->recv_frame_header.type == HTTP2_FRAME_TYPE_PING)
		{
			return HTTP2_Frame_Ping_Process(worker_id, conn);
		}
		// else ignore frame
	}

	return HTTP2_CONNECTION_OK;
}
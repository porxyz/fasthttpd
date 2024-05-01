#include "http2_stream_processor.h"
#include "http_worker.h"

#include "../server_log.h"
#include "../server_config.h"
#include "../helper_functions.h"
#include "../endianness_conversions.h"

#include <cstring>
#include <cstdlib>

#include <unistd.h>
#include <sys/socket.h>

int HTTP2_Stream_Init(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id)
{
	struct HTTP2_CONNECTION *http2_conn = (struct HTTP2_CONNECTION*) conn->raw_connection;

	// client wants to create more streams than the limit
	if (http2_conn->streams.size() + 1 >= http2_conn->server_settings.max_concurrent_streams)
	{
		return HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_ENHANCE_YOUR_CALM, 0, "too many opened streams");
	}

	struct HTTP2_STREAM current_stream;
	current_stream.state = HTTP2_STREAM_STATE_WAIT_HEADERS;

	current_stream.send_buffer_offset = 0;

	current_stream.file_transfer.file_descriptor = -1;

	current_stream.expected_request_body_size = 0;

	current_stream.recv_window_avail_bytes = http2_conn->server_settings.init_window_size;
	current_stream.send_window_avail_bytes = http2_conn->client_settings.init_window_size;

	current_stream.request.COOKIES = NULL;
	current_stream.request.POST_query = NULL;
	current_stream.request.POST_files = NULL;
	current_stream.request.POST_type = HTTP_POST_TYPE_UNDEFINED;

	current_stream.response.COOKIES = NULL;

	http2_conn->streams[stream_id] = current_stream;

	total_http_connections++;

	if (is_server_load_balancer_fair)
	{
		connections_per_worker[worker_id]++;
	}

	return HTTP2_CONNECTION_OK;
}

int HTTP2_Stream_Send_Headers(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id)
{
	struct HTTP2_CONNECTION* http2_conn = (struct HTTP2_CONNECTION*) conn->raw_connection;
	struct HTTP2_STREAM &current_stream = http2_conn->streams[stream_id];

	unsigned int max_frame_size = http2_conn->client_settings.max_frame_size;
	unsigned int current_frame_size;

	bool last_header_frame = false;
	while (!last_header_frame)
	{
		current_frame_size = current_stream.send_buffer.size() - current_stream.send_buffer_offset;

		if (current_frame_size > max_frame_size)
		{
			current_frame_size = max_frame_size;
		}

		last_header_frame = (current_frame_size + current_stream.send_buffer_offset) == current_stream.send_buffer.size();
		bool first_header_frame = current_stream.send_buffer_offset == 0;
		bool last_frame = last_header_frame and current_stream.response.body.empty() and current_stream.state != HTTP2_STREAM_STATE_FILE_BOUND;

		struct HTTP2_FRAME_CONTAINER frame_container;
		frame_container.length = sizeof(struct HTTP2_FRAME_HEADER) + current_frame_size;
		frame_container.contents = new (std::nothrow) uint8_t[frame_container.length];

		if (!frame_container.contents)
		{
			SERVER_ERROR_LOG_stdlib_err("Unable to allocate memory for a HTTP2 frame.");
			exit(-1);
		}

		struct HTTP2_FRAME_HEADER *frame_header = (struct HTTP2_FRAME_HEADER *)frame_container.contents;
		frame_header->stream_id = endian_conv_hton32(stream_id);
		frame_header->length = endian_conv_hton24(current_frame_size);

		if(first_header_frame)
		{
			frame_header->type = HTTP2_FRAME_TYPE_HEADERS;
		}
		else
		{
			frame_header->type = HTTP2_FRAME_TYPE_CONTINUATION;
		}

		frame_header->flags = 0;
		if (last_header_frame)
		{
			frame_header->flags = HTTP2_FRAME_FLAG_END_HEADERS;
		}

		if (last_frame)
		{
			frame_header->flags |= HTTP2_FRAME_FLAG_END_STREAM;
		}

		memcpy(frame_container.contents + sizeof(struct HTTP2_FRAME_HEADER), current_stream.send_buffer.c_str() + current_stream.send_buffer_offset, current_frame_size);
		http2_conn->frame_queue.push_back(frame_container);

		current_stream.send_buffer_offset += current_frame_size;

		// headers fully enqueued to be send
		if (last_header_frame and current_stream.state != HTTP2_STREAM_STATE_FILE_BOUND)
		{
			current_stream.send_buffer = current_stream.response.body;
			current_stream.response.body.clear();
			current_stream.send_buffer_offset = 0;
			current_stream.state = HTTP2_STREAM_STATE_CONTENT_BOUND;
		}

		// http request complete
		if (last_frame)
		{	
			HTTP2_Stream_Delete(worker_id, http2_conn, stream_id);
		}
	}

	//Force send frames
	return HTTP2_Connection_Send_Enqueued_Frames(worker_id, conn);
}

void HTTP2_Stream_Send_Body(const int worker_id, struct HTTP2_CONNECTION *http2_conn, const uint32_t stream_id)
{
	struct HTTP2_STREAM& current_stream = http2_conn->streams[stream_id];

	int32_t max_frame_size = http2_conn->client_settings.max_frame_size;
	if (max_frame_size > current_stream.send_window_avail_bytes)
	{
		max_frame_size = current_stream.send_window_avail_bytes;
	}

	// not enough bytes available in the client window
	if (max_frame_size <= 0)
	{
		return;
	}

	int32_t current_frame_size = (int32_t) (current_stream.send_buffer.size() - current_stream.send_buffer_offset);
	if (current_frame_size > max_frame_size)
	{
		current_frame_size = max_frame_size;
	}

	bool last_frame = ((size_t)current_frame_size + current_stream.send_buffer_offset) == current_stream.send_buffer.size();

	HTTP2_FRAME_CONTAINER frame_container;
	frame_container.length = sizeof(struct HTTP2_FRAME_HEADER) + current_frame_size;
	frame_container.contents = new (std::nothrow) uint8_t[frame_container.length];

	if (!frame_container.contents)
	{
		SERVER_ERROR_LOG_stdlib_err("Unable to allocate memory for a HTTP2 frame.");
		exit(-1);
	}

	struct HTTP2_FRAME_HEADER *frame_header = (struct HTTP2_FRAME_HEADER *)frame_container.contents;
	frame_header->stream_id = endian_conv_hton32(stream_id);
	frame_header->length = endian_conv_hton24(current_frame_size);
	frame_header->type = HTTP2_FRAME_TYPE_DATA;
	frame_header->flags = 0;

	if (last_frame)
	{
		frame_header->flags = HTTP2_FRAME_FLAG_END_STREAM;
	}

	memcpy(frame_container.contents + sizeof(struct HTTP2_FRAME_HEADER), current_stream.send_buffer.c_str() + current_stream.send_buffer_offset, current_frame_size);
	http2_conn->frame_queue.push_back(frame_container);

	current_stream.send_buffer_offset += current_frame_size;
	current_stream.send_window_avail_bytes -= current_frame_size;

	// http request complete
	if (last_frame)
	{
		HTTP2_Stream_Delete(worker_id, http2_conn, stream_id);
	}
}

int HTTP2_Stream_Send_From_File(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id)
{
	struct HTTP2_CONNECTION *http2_conn = (struct HTTP2_CONNECTION *)conn->raw_connection;
	struct HTTP2_STREAM &current_stream = http2_conn->streams[stream_id];

	uint32_t max_read_size = http2_conn->client_settings.max_frame_size;
	if (max_read_size > current_stream.send_window_avail_bytes)
	{
		max_read_size = current_stream.send_window_avail_bytes;
	}

	// not enough bytes available in the client window
	if (max_read_size <= 0)
	{
		return HTTP2_CONNECTION_OK;
	}

	uint32_t read_buffer_size = str2uint(SERVER_CONFIGURATION["read_buffer_size"].c_str()) * 1024;

	if(max_read_size > read_buffer_size)
	{
		max_read_size = read_buffer_size;
	}

	ssize_t read_bytes;
	uint64_t bytes_to_read, file_len;

	file_len = current_stream.file_transfer.stop_offset - current_stream.file_transfer.file_offset;
	bytes_to_read = (file_len > max_read_size) ? max_read_size : file_len;

	bool should_stop = false;
	while (!should_stop)
	{
		read_bytes = pread(current_stream.file_transfer.file_descriptor, http_workers[worker_id].recv_buffer, bytes_to_read, current_stream.file_transfer.file_offset);

		if (read_bytes == -1)
		{
			if (errno == EINTR)
			{
				continue;
			}
			else
			{
				std::string err_msg = "Unable to read from requested file (";
				err_msg.append(current_stream.request.URI_path);
				err_msg.append(" )");

				SERVER_ERROR_LOG_stdlib_err(err_msg.c_str());

				return HTTP2_Stream_Reset(worker_id, conn, stream_id, HTTP2_ERROR_CODE_INTERNAL_ERROR);
			}
		}

		should_stop = true;
	}

	current_stream.file_transfer.file_offset += read_bytes;
	current_stream.send_window_avail_bytes -= read_bytes;

	bool last_frame = current_stream.file_transfer.file_offset == current_stream.file_transfer.stop_offset;

	HTTP2_FRAME_CONTAINER frame_container;
	frame_container.length = sizeof(struct HTTP2_FRAME_HEADER) + read_bytes;
	frame_container.contents = new (std::nothrow) uint8_t[frame_container.length];

	if (!frame_container.contents)
	{
		SERVER_ERROR_LOG_stdlib_err("Unable to allocate memory for a HTTP2 frame.");
		exit(-1);
	}

	struct HTTP2_FRAME_HEADER *frame_header = (struct HTTP2_FRAME_HEADER *)frame_container.contents;
	frame_header->stream_id = endian_conv_hton32(stream_id);
	frame_header->length = endian_conv_hton24(read_bytes);
	frame_header->type = HTTP2_FRAME_TYPE_DATA;
	frame_header->flags = 0;

	if (last_frame)
	{
		frame_header->flags = HTTP2_FRAME_FLAG_END_STREAM;
	}

	memcpy(frame_container.contents + sizeof(struct HTTP2_FRAME_HEADER), http_workers[worker_id].recv_buffer, read_bytes);
	http2_conn->frame_queue.push_back(frame_container);

	// http request complete
	if (last_frame)
	{
		HTTP2_Stream_Delete(worker_id, http2_conn, stream_id);
	}

	return HTTP2_CONNECTION_OK;
}

int HTTP2_Stream_Reset(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id, const uint32_t error_code)
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
	frame_header->type = HTTP2_FRAME_TYPE_RESET_STREAM;
	frame_header->flags = 0;
	frame_header->length = endian_conv_hton24(4);

	uint32_t* err_code = (uint32_t*)  (frame_container.contents + sizeof(struct HTTP2_FRAME_HEADER));
	*err_code = endian_conv_hton32(error_code);

	http2_conn->frame_queue.push_back(frame_container);

	HTTP2_Stream_Delete(worker_id, http2_conn, stream_id);

	return HTTP2_Connection_Send_Enqueued_Frames(worker_id, conn);
}

void HTTP2_Stream_Delete(const int worker_id, struct HTTP2_CONNECTION *http2_conn, const uint32_t stream_id)
{
	struct HTTP2_STREAM& stream = http2_conn->streams[stream_id];

	if(stream.request.COOKIES)
	{
		delete(stream.request.COOKIES);
	}

	if(stream.request.POST_query)
	{
		delete(stream.request.POST_query);
	}

	if(stream.request.POST_files)
	{
		delete(stream.request.POST_files);
	}

	if(stream.response.COOKIES)
	{
		delete(stream.response.COOKIES);
	}

	if(stream.state == HTTP2_STREAM_STATE_FILE_BOUND and stream.file_transfer.file_descriptor != -1)
	{
		close(stream.file_transfer.file_descriptor);
	}

	http2_conn->streams.erase(stream_id);

	total_http_connections--;

	if(is_server_load_balancer_fair)
	{
		connections_per_worker[worker_id]--;
	}
}
#include "http2_connection_processor.h"
#include "http_worker.h"

#include "../helper_functions.h"
#include "../server_config.h"
#include "../server_log.h"
#include "../endianness_conversions.h"

#include <cstring>
#include <cstdlib>

void HTTP2_Connection_Init(struct HTTP2_CONNECTION *http2_conn)
{
	// load default fail-safe values for the client settings
	http2_conn->client_settings.hpack_table_size = HTTP2_SETTINGS_HPACK_TABLE_SIZE_MIN;
	http2_conn->client_settings.enable_push = 0;
	http2_conn->client_settings.max_concurrent_streams = 100;
	http2_conn->client_settings.init_window_size = 65535;  // 2^16 - 1
	http2_conn->client_settings.max_frame_size = HTTP2_SETTINGS_MAX_FRAME_SIZE_MIN;	
	http2_conn->client_settings.max_header_list_size = 65535; // 2^16 - 1

	// load default server settings
	http2_conn->server_settings.hpack_table_size = 4096;
	http2_conn->server_settings.enable_push = 0;
	http2_conn->server_settings.max_concurrent_streams = str2uint(SERVER_CONFIGURATION["http2_max_concurrent_streams"]);
	http2_conn->server_settings.init_window_size = str2uint(SERVER_CONFIGURATION["http2_init_window_size"]) * 1024;
	http2_conn->server_settings.max_frame_size = str2uint(SERVER_CONFIGURATION["http2_max_frame_size"]) * 1024;
	http2_conn->server_settings.max_header_list_size = 65535;

	// init HPACK encoder/decoder context
	int result = nghttp2_hd_deflate_new(&http2_conn->hpack_encoder, http2_conn->server_settings.hpack_table_size);
	if (result != 0)
	{
		SERVER_LOG_WRITE_ERROR.lock();
		SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING), true);
		SERVER_LOG_WRITE(" Unable to init HPACK encoder.\nError code: 0x", true);
		SERVER_LOG_WRITE(std::hex, true);
		SERVER_LOG_WRITE(result, true);
		SERVER_LOG_WRITE("\n", true);
		SERVER_LOG_WRITE(nghttp2_strerror(result), true);
		SERVER_LOG_WRITE("\n\n", true);
		SERVER_LOG_WRITE(std::dec, true);
		SERVER_LOG_WRITE_ERROR.unlock();
		
		exit(-1);
	}

	result = nghttp2_hd_inflate_new(&http2_conn->hpack_decoder);
	if (result != 0)
	{
		SERVER_LOG_WRITE_ERROR.lock();
		SERVER_LOG_WRITE(SERVER_LOG_strtime(SERVER_LOG_LOCALTIME_REPORTING), true);
		SERVER_LOG_WRITE(" Unable to init HPACK decoder.\nError code: 0x", true);
		SERVER_LOG_WRITE(std::hex, true);
		SERVER_LOG_WRITE(result, true);
		SERVER_LOG_WRITE("\n", true);
		SERVER_LOG_WRITE(nghttp2_strerror(result), true);
		SERVER_LOG_WRITE("\n\n", true);
		SERVER_LOG_WRITE(std::dec, true);
		SERVER_LOG_WRITE_ERROR.unlock();
		
		exit(-1);
	}

	http2_conn->frame_header_is_recv = false;
	http2_conn->recv_window_avail_bytes = http2_conn->server_settings.init_window_size;

	http2_conn->send_buffer_len = 0;
	http2_conn->send_buffer_offset = 0;
	http2_conn->send_window_avail_bytes = http2_conn->client_settings.init_window_size;
}

void HTTP2_Connection_Insert_Frame(struct HTTP2_CONNECTION *http2_conn, const uint32_t data_len, uint8_t *contents, bool alloc_mem)
{
	struct HTTP2_FRAME_CONTAINER frame_container;
	frame_container.length = data_len;

	if (alloc_mem)
	{
		frame_container.contents = new (std::nothrow) uint8_t[data_len];
		if (!frame_container.contents)
		{
			SERVER_ERROR_LOG_stdlib_err("Unable to allocate memory for a HTTP2 frame.");
			exit(-1);
		}

		memcpy(frame_container.contents, contents, data_len);
	}
	else
	{
		frame_container.contents = contents;
	}

	http2_conn->frame_queue.push_back(frame_container);
}

int HTTP2_Connection_Send_Enqueued_Frames(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn)
{
	struct HTTP2_CONNECTION *http2_conn = (struct HTTP2_CONNECTION *)conn->raw_connection;

	if(conn->state == HTTP2_CONNECTION_STATE_WAIT_HELLO)
	{
		return HTTP2_CONNECTION_OK;
	}

	bool send_loop_should_stop = false;
	while (!send_loop_should_stop)
	{
		// no frame in buffer
		if (http2_conn->send_buffer_len == 0)
		{
			if (!http2_conn->frame_queue.empty())
			{
				struct HTTP2_FRAME_CONTAINER frame_container = http2_conn->frame_queue.front();
				struct HTTP2_FRAME_HEADER *frame_header = (struct HTTP2_FRAME_HEADER *)frame_container.contents;

				unsigned int frame_len = frame_container.length - sizeof(struct HTTP2_FRAME_HEADER);

				// if frame type is data, we need to work with the control flow window
				if (frame_header->type == HTTP2_FRAME_TYPE_DATA)
				{
					// only send a data frame if the client window is big enough
					if (http2_conn->send_window_avail_bytes >= frame_len)
					{
						http2_conn->frame_queue.pop_front();

						http2_conn->send_buffer_offset = 0;
						http2_conn->send_buffer_len = frame_container.length;
						http2_conn->send_buffer = frame_container.contents;

						http2_conn->send_window_avail_bytes -= frame_len;
					}
					else
					{
						send_loop_should_stop = true;
						continue;
					}
				}
				else
				{
					http2_conn->frame_queue.pop_front();

					http2_conn->send_buffer_offset = 0;
					http2_conn->send_buffer_len = frame_container.length;
					http2_conn->send_buffer = frame_container.contents;
				}
			}
			else // no data to send && no frame in queue
			{
				send_loop_should_stop = true;
				continue;
			}
		}

		if (http2_conn->send_buffer_len != 0)
		{
			int written_bytes = Network_Write_Bytes(conn, http2_conn->send_buffer + http2_conn->send_buffer_offset, http2_conn->send_buffer_len - http2_conn->send_buffer_offset);
			if (written_bytes == 0)
			{
				send_loop_should_stop = true;
				continue;
			}
			else if (written_bytes < 0)
			{
				Generic_Connection_Delete(worker_id, conn);
				return HTTP2_CONNECTION_DELETED;
			}

			http2_conn->send_buffer_offset += written_bytes;

			// frame was send successfully
			if (http2_conn->send_buffer_offset == http2_conn->send_buffer_len)
			{
				struct HTTP2_FRAME_HEADER *frame_header = (struct HTTP2_FRAME_HEADER *)http2_conn->send_buffer;

				uint32_t stream_id = endian_conv_ntoh_http31(frame_header->stream_id);

				// headers was completely sent
				// requests without data are ignored (eg HEAD)
				if ( (frame_header->type == HTTP2_FRAME_TYPE_HEADERS or frame_header->type == HTTP2_FRAME_TYPE_CONTINUATION)
				      and (frame_header->flags & HTTP2_FRAME_FLAG_END_HEADERS) and !(frame_header->flags & HTTP2_FRAME_FLAG_END_STREAM) )
				{
					if(HTTP_Request_Process(worker_id, conn, stream_id) == HTTP2_CONNECTION_DELETED)
					{
						return HTTP2_CONNECTION_DELETED;
					}
				}
				else if(frame_header->type == HTTP2_FRAME_TYPE_DATA)
				{
					if (http2_conn->streams.find(stream_id) != http2_conn->streams.end())
					{
						if(HTTP_Request_Process(worker_id, conn, stream_id) == HTTP2_CONNECTION_DELETED)
						{
							return HTTP2_CONNECTION_DELETED;
						}
					}
				}

				else if(frame_header->type == HTTP2_FRAME_TYPE_GOAWAY)
				{
					Generic_Connection_Delete(worker_id, conn);
					return HTTP2_CONNECTION_DELETED;
				}

				// free memory && reset counters
				delete[] http2_conn->send_buffer;
				http2_conn->send_buffer = NULL;
				http2_conn->send_buffer_len = 0;
				http2_conn->send_buffer_offset = 0;
			}
		}
	}

	return HTTP2_CONNECTION_OK;
}

void HTTP2_Connection_Recv_Data(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, std::string* temp_buff)
{
	//do not process additional frames for state = error
	if(conn->state == HTTP2_CONNECTION_STATE_ERROR)
	{
		return;
	}

	struct HTTP2_CONNECTION *http2_conn = (struct HTTP2_CONNECTION *) conn->raw_connection;

	size_t temp_buff_offset = 0;
	size_t max_read_size = str2uint(SERVER_CONFIGURATION["read_buffer_size"].c_str()) * 1024;

	bool recv_stop = false;
	while (!recv_stop)
	{
		if (!http2_conn->frame_header_is_recv)
		{
			size_t recv_buffer_offset = http2_conn->recv_buffer.size();

			// feed already recv data from an local buffer
			// useful for upgrade from http/1.1
			if(temp_buff)
			{
				size_t needed_bytes = sizeof(struct HTTP2_FRAME_HEADER) - recv_buffer_offset;
				if(needed_bytes > temp_buff->size() - temp_buff_offset)
				{
					needed_bytes = temp_buff->size() - temp_buff_offset;
				}

				http2_conn->recv_buffer.append(temp_buff->c_str() + temp_buff_offset, needed_bytes);
				recv_buffer_offset += needed_bytes;
				temp_buff_offset += needed_bytes;

				//the local buffer is fully consumed
				//switch back to reading from the network
				if(temp_buff_offset == temp_buff->size())
				{
					temp_buff = NULL;
				}
			}
			else
			{
				int32_t read_bytes = Network_Read_Bytes(conn, http_workers[worker_id].recv_buffer, sizeof(struct HTTP2_FRAME_HEADER) - recv_buffer_offset);
				if (read_bytes == 0)
				{
					recv_stop = true;
					continue;
				}
				else if (read_bytes < 0)
				{
					Generic_Connection_Delete(worker_id, conn);
					return;
				}

				http2_conn->recv_buffer.append(http_workers[worker_id].recv_buffer, read_bytes);
				recv_buffer_offset += read_bytes;
			}

			if (recv_buffer_offset == sizeof(struct HTTP2_FRAME_HEADER))
			{
				struct HTTP2_FRAME_HEADER *buff_frame_header = (struct HTTP2_FRAME_HEADER *)http2_conn->recv_buffer.c_str();

				// read the frame header
				http2_conn->recv_frame_header.length = endian_conv_ntoh24(buff_frame_header->length);
				http2_conn->recv_frame_header.type = buff_frame_header->type;
				http2_conn->recv_frame_header.flags = buff_frame_header->flags;
				http2_conn->recv_frame_header.stream_id = endian_conv_ntoh_http31(buff_frame_header->stream_id);

				http2_conn->recv_buffer.clear(); // reset the buffer
				http2_conn->frame_header_is_recv = true; // we have the frame header

				if(http2_conn->recv_frame_header.length == 0)
				{	
					//valid settings frame with ACK and len = 0
					if(http2_conn->recv_frame_header.type == HTTP2_FRAME_TYPE_SETTINGS and (http2_conn->recv_frame_header.flags & HTTP2_FRAME_FLAG_ACK))
					{
						http2_conn->frame_header_is_recv = false;
						continue;
					}

					HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_FRAME_SIZE_ERROR, http2_conn->recv_frame_header.stream_id , "frame size can't be 0");
					return;
				}
				else if(http2_conn->recv_frame_header.length > http2_conn->server_settings.max_frame_size)
				{	
					HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_FRAME_SIZE_ERROR, http2_conn->recv_frame_header.stream_id , "frame is too big");
					return;
				}
				else if(http2_conn->recv_frame_header.type == HTTP2_FRAME_TYPE_DATA and http2_conn->recv_frame_header.length > http2_conn->recv_window_avail_bytes)
				{	
					HTTP2_Connection_Error(worker_id, conn, HTTP2_ERROR_CODE_FLOW_CONTROL_ERROR, http2_conn->recv_frame_header.stream_id , "data frame is bigger than control window");
					return;
				}
			}
		}

		if (http2_conn->frame_header_is_recv)
		{
			size_t recv_buffer_offset = http2_conn->recv_buffer.size();

			if(temp_buff) 
			{
				size_t needed_bytes = http2_conn->recv_frame_header.length - recv_buffer_offset;
				if(needed_bytes > temp_buff->size() - temp_buff_offset)
				{
					needed_bytes = temp_buff->size() - temp_buff_offset;
				}

				http2_conn->recv_buffer.append(temp_buff->c_str() + temp_buff_offset, needed_bytes);
				recv_buffer_offset += needed_bytes;
				temp_buff_offset += needed_bytes;

				if(temp_buff_offset == temp_buff->size())
				{
					temp_buff = NULL;
				}
			}
			else
			{
				size_t bytes_to_read =  http2_conn->recv_frame_header.length - recv_buffer_offset;
				if(bytes_to_read > max_read_size)
				{
					bytes_to_read = max_read_size;
				}

				int32_t read_bytes = Network_Read_Bytes(conn, http_workers[worker_id].recv_buffer, bytes_to_read);
				if (read_bytes == 0)
				{
					recv_stop = true;
					continue;
				}
				else if (read_bytes < 0)
				{
					Generic_Connection_Delete(worker_id, conn);
					return;
				}

				http2_conn->recv_buffer.append(http_workers[worker_id].recv_buffer, read_bytes);
				recv_buffer_offset += read_bytes;
			}

			if (recv_buffer_offset == http2_conn->recv_frame_header.length)
			{
				// window update for data frame
				if (http2_conn->recv_frame_header.type == HTTP2_FRAME_TYPE_DATA)
				{
					http2_conn->recv_window_avail_bytes -= http2_conn->recv_frame_header.length;
					const int32_t init_window_size = http2_conn->server_settings.init_window_size;

					if (http2_conn->recv_window_avail_bytes <= init_window_size / 2)
					{
						uint32_t window_increment = http2_conn->server_settings.init_window_size - http2_conn->recv_window_avail_bytes;
						http2_conn->recv_window_avail_bytes += window_increment;

						if(HTTP2_Frame_Window_Update_Generate(worker_id, conn, 0, window_increment) == HTTP2_CONNECTION_DELETED)
						{
							return;
						}
					}
				}

				if(HTTP2_Frame_Process(worker_id, conn) == HTTP2_CONNECTION_DELETED)
				{
					return;
				}
				
				// frame is processed
				http2_conn->recv_buffer.clear();
				http2_conn->frame_header_is_recv = false;
			}
		}
	}
}

void HTTP2_Connection_Process(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, std::string* temp_buff)
{
	struct HTTP2_CONNECTION *http2_conn = (struct HTTP2_CONNECTION *)conn->raw_connection;

	if (conn->state == HTTP2_CONNECTION_STATE_WAIT_HELLO)
	{
		size_t recv_buffer_offset = http2_conn->recv_buffer.size();

		int32_t read_bytes = Network_Read_Bytes(conn, http_workers[worker_id].recv_buffer, 24 - recv_buffer_offset);
		if (read_bytes < 0)
		{
			Generic_Connection_Delete(worker_id, conn);
			return;
		}

		recv_buffer_offset += read_bytes;
		http2_conn->recv_buffer.append(http_workers[worker_id].recv_buffer, read_bytes);

		if (recv_buffer_offset >= 24) // http2 hello len
		{
			if (memcmp(HTTP2_magic_hello, http2_conn->recv_buffer.c_str(), 24) == 0)
			{
				conn->state = HTTP2_CONNECTION_STATE_WAIT_SETTINGS;
				http2_conn->recv_buffer.clear();

				HTTP2_Frame_Settings_Generate(http2_conn);
			}
			else //malformed hello
			{
				Generic_Connection_Delete(worker_id, conn);
				return;
			}
		}
		else
		{
			return;
		}
	}

	if(HTTP2_Connection_Send_Enqueued_Frames(worker_id, conn) == HTTP2_CONNECTION_OK)
	{
		HTTP2_Connection_Recv_Data(worker_id, conn, temp_buff);
	}
}

int HTTP2_Connection_Error(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t error_code, const uint32_t last_stream_id, const char *additional_info)
{
	struct HTTP2_CONNECTION *http2_conn = (struct HTTP2_CONNECTION *) conn->raw_connection;

	uint32_t additional_info_len = (additional_info) ? strlen(additional_info) : 0;

	struct HTTP2_FRAME_CONTAINER frame_container;
	frame_container.length = 8 + additional_info_len + sizeof(struct HTTP2_FRAME_HEADER);

	frame_container.contents = new (std::nothrow) uint8_t[frame_container.length];
	if (!frame_container.contents)
	{
		SERVER_ERROR_LOG_stdlib_err("Unable to allocate memory for a HTTP2 frame.");
		exit(-1);
	}

	struct HTTP2_FRAME_HEADER *error_frame_header = (struct HTTP2_FRAME_HEADER *)frame_container.contents;
	error_frame_header->stream_id = 0;
	error_frame_header->flags = 0;
	error_frame_header->type = HTTP2_FRAME_TYPE_GOAWAY;
	error_frame_header->length = endian_conv_hton24(8 + additional_info_len);

	uint32_t *frame_data = (uint32_t *)(frame_container.contents + sizeof(struct HTTP2_FRAME_HEADER));
	frame_data[0] = endian_conv_hton32(last_stream_id);
	frame_data[1] = endian_conv_hton32(error_code);

	if (additional_info)
	{
		memcpy(frame_container.contents + sizeof(struct HTTP2_FRAME_HEADER) + 8, additional_info, additional_info_len);
	}

	http2_conn->frame_queue.push_front(frame_container);

	conn->state = HTTP2_CONNECTION_STATE_ERROR;

	return HTTP2_Connection_Send_Enqueued_Frames(worker_id, conn);
}

void HTTP2_Connection_Delete(const int worker_id, struct HTTP2_CONNECTION* http2_conn)
{
	//delete the current frame in buffer
	if(http2_conn->send_buffer)
	{
		delete[] http2_conn->send_buffer;
		http2_conn->send_buffer = NULL;
	}

	//delete the enqueued frames
	for(auto i = http2_conn->frame_queue.begin(); i != http2_conn->frame_queue.end(); i++)
	{
		delete[] (*i).contents;
	}

	for(auto it = http2_conn->streams.begin(); it != http2_conn->streams.end(); it++)
	{
		auto temp_it = it;
		it++;

		HTTP2_Stream_Delete(worker_id, http2_conn, temp_it->first);

		if(it == http2_conn->streams.end())
		{
			break;
		}
	}

	//delete the HPACK contexts
	nghttp2_hd_inflate_del(http2_conn->hpack_decoder);
	nghttp2_hd_deflate_del(http2_conn->hpack_encoder);
}
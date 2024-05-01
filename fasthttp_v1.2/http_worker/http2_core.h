#ifndef __http2_core__inc
#define __http2_core__inc

#include "http_core.h"

#include <list>

#include <nghttp2/nghttp2.h>

#define HTTP2_FRAME_TYPE_DATA 0
#define HTTP2_FRAME_TYPE_HEADERS 1
#define HTTP2_FRAME_TYPE_PRIORITY 2
#define HTTP2_FRAME_TYPE_RESET_STREAM 3
#define HTTP2_FRAME_TYPE_SETTINGS 4
#define HTTP2_FRAME_TYPE_PUSH_PROMISE 5
#define HTTP2_FRAME_TYPE_PING 6
#define HTTP2_FRAME_TYPE_GOAWAY 7
#define HTTP2_FRAME_TYPE_WINDOW_UPDATE 8
#define HTTP2_FRAME_TYPE_CONTINUATION 9
#define HTTP2_FRAME_TYPE_ALTSVC 0xA
#define HTTP2_FRAME_TYPE_ORIGIN 0xC
#define HTTP2_FRAME_TYPE_PRIORITY_UPDATE 0x10

#define HTTP2_SETTINGS_HPACK_TABLE_SIZE 1
#define HTTP2_SETTINGS_ENABLE_PUSH 2
#define HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS 3
#define HTTP2_SETTINGS_INITIAL_WINDOW_SIZE 4
#define HTTP2_SETTINGS_MAX_FRAME_SIZE 5
#define HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE 6

#define HTTP2_SETTINGS_HPACK_TABLE_SIZE_MIN 4096
#define HTTP2_SETTINGS_MAX_FRAME_SIZE_MIN 16383 // 2^14 - 1
#define HTTP2_SETTINGS_MAX_FRAME_SIZE_MAX 16777215 // 2^24 - 1
#define HTTP2_SETTINGS_INITIAL_WINDOW_SIZE_MIN HTTP2_SETTINGS_MAX_FRAME_SIZE_MIN
#define HTTP2_SETTINGS_INITIAL_WINDOW_SIZE_MAX 2147483647 // 2^31 - 1


#define HTTP2_FRAME_FLAG_ACK 1
#define HTTP2_FRAME_FLAG_END_STREAM 1
#define HTTP2_FRAME_FLAG_END_HEADERS 4
#define HTTP2_FRAME_FLAG_PADDED 8
#define HTTP2_FRAME_FLAG_PRIORITY 0x20

#define HTTP2_ERROR_CODE_NO_ERROR 0
#define HTTP2_ERROR_CODE_PROTOCOL_ERROR 1
#define HTTP2_ERROR_CODE_INTERNAL_ERROR 2
#define HTTP2_ERROR_CODE_FLOW_CONTROL_ERROR 3
#define HTTP2_ERROR_CODE_SETTINGS_TIMEOUT 4
#define HTTP2_ERROR_CODE_STREAM_CLOSED 5 
#define HTTP2_ERROR_CODE_FRAME_SIZE_ERROR 6 
#define HTTP2_ERROR_CODE_REFUSED_STREAM 7 
#define HTTP2_ERROR_CODE_CANCEL 8
#define HTTP2_ERROR_CODE_COMPRESSION_ERROR 9
#define HTTP2_ERROR_CODE_CONNECT_ERROR 0xa
#define HTTP2_ERROR_CODE_ENHANCE_YOUR_CALM 0xb
#define HTTP2_ERROR_CODE_INADEQUATE_SECURITY 0xc
#define HTTP2_ERROR_CODE_HTTP_1_1_REQUIRED 0xd

#define HTTP2_CONNECTION_STATE_WAIT_HELLO 0
#define HTTP2_CONNECTION_STATE_WAIT_SETTINGS 1
#define HTTP2_CONNECTION_STATE_NORMAL 2
#define HTTP2_CONNECTION_STATE_ERROR 3

#define HTTP2_CONNECTION_OK 0
#define HTTP2_CONNECTION_DELETED -1

#define HTTP_CONNECTION_OK HTTP2_CONNECTION_OK
#define HTTP_CONNECTION_DELETED HTTP2_CONNECTION_DELETED

#define HTTP2_STREAM_STATE_WAIT_HEADERS 0
#define HTTP2_STREAM_STATE_WAIT_BODY 1
#define HTTP2_STREAM_STATE_PROCESSING 2
#define HTTP2_STREAM_STATE_SEND_HEADERS 3
#define HTTP2_STREAM_STATE_FILE_BOUND 4
#define HTTP2_STREAM_STATE_CONTENT_BOUND 5
#define HTTP2_STREAM_STATE_CUSTOM_BOUND 6

#define HTTP2_magic_hello "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"

struct HTTP2_FRAME_HEADER
{
	unsigned int length : 24;
	unsigned int type : 8;
	unsigned int flags : 8;
	
	//node: the most significant bit is reserved
	unsigned int stream_id : 32;
} __attribute__((packed, aligned(1)));

struct HTTP2_SETTINGS_PARAMETER
{
	uint16_t id;
	uint32_t value;
} __attribute__((packed, aligned(1)));

struct HTTP2_CONNECTION_SETTINGS
{
	uint32_t hpack_table_size;
	uint32_t enable_push;
	uint32_t max_concurrent_streams;
	uint32_t init_window_size;
	uint32_t max_frame_size;
	uint32_t max_header_list_size;
};

struct HTTP2_FRAME_CONTAINER
{
	uint32_t length;
	uint8_t* contents; //this includes header too
};

struct HTTP2_STREAM
{
	int state;
	
	std::string recv_buffer;
	int64_t recv_window_avail_bytes;
	
	std::string send_buffer;
	uint64_t send_buffer_offset;
	int64_t send_window_avail_bytes;
	
	struct HTTP_REQUEST request;
	struct HTTP_RESPONSE response;

	uint64_t expected_request_body_size;

	struct HTTP_FILE_TRANSFER file_transfer;
};

struct HTTP2_CONNECTION
{
	struct HTTP2_CONNECTION_SETTINGS client_settings;
	struct HTTP2_CONNECTION_SETTINGS server_settings;
	
	std::unordered_map<uint32_t, struct HTTP2_STREAM> streams;
	
	bool frame_header_is_recv;
	struct HTTP2_FRAME_HEADER recv_frame_header;
	
	nghttp2_hd_deflater* hpack_encoder;
	nghttp2_hd_inflater* hpack_decoder;
	
	std::string recv_buffer;
	int64_t recv_window_avail_bytes;
	
	uint8_t* send_buffer;
	uint32_t send_buffer_len;
	uint32_t send_buffer_offset;
	int64_t send_window_avail_bytes;
	
	std::list<struct HTTP2_FRAME_CONTAINER> frame_queue;
};


static const uint8_t HTTP2_Frame_Settings_ACK[] = {
	0, 0, 0, // len = 0
	HTTP2_FRAME_TYPE_SETTINGS,
	HTTP2_FRAME_FLAG_ACK, //ACK_FLAG
	0, 0, 0, 0 // stream id
};

#endif

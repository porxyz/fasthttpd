#ifndef __http2_connection_processor_inc__
#define __http2_connection_processor_inc__

#include "http2_core.h"

void HTTP2_Connection_Init(struct HTTP2_CONNECTION *http2_conn);
void HTTP2_Connection_Insert_Frame(struct HTTP2_CONNECTION *http2_conn, const uint32_t data_len, uint8_t *contents, bool alloc_mem = true);
int HTTP2_Connection_Send_Enqueued_Frames(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn);
void HTTP2_Connection_Recv_Data(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, std::string* temp_buff);
int HTTP2_Connection_Error(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t error_code, const uint32_t last_stream_id, const char *additional_info = NULL);
void HTTP2_Connection_Process(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, std::string* temp_buff = NULL);
void HTTP2_Connection_Delete(const int worker_id, struct HTTP2_CONNECTION* http2_conn);

#endif
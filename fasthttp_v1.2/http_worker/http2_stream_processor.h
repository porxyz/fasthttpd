#ifndef __http2_stream_processor_inc__
#define __http2_stream_processor_inc__

#include "http2_core.h"

int HTTP2_Stream_Init(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id);
int HTTP2_Stream_Send_Headers(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id);
void HTTP2_Stream_Send_Body(const int worker_id, struct HTTP2_CONNECTION *http2_conn, const uint32_t stream_id);
int HTTP2_Stream_Send_From_File(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id);
int HTTP2_Stream_Reset(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id, const uint32_t error_code);
void HTTP2_Stream_Delete(const int worker_id, struct HTTP2_CONNECTION *http2_conn, const uint32_t stream_id);

#endif
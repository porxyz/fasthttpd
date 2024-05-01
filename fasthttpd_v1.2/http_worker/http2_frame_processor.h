#ifndef __http2_frame_processor_inc__
#define __http2_frame_processor_inc__

#include "http2_core.h"

int HTTP2_Frame_Settings_Process(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn);
void HTTP2_Frame_Settings_Generate(struct HTTP2_CONNECTION* http2_conn);
int HTTP2_Frame_Window_Update_Process(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn);
int HTTP2_Frame_Window_Update_Generate(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id, const uint32_t increment);
int HTTP2_Frame_Header_Process(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn);
int HTTP2_Frame_Data_Process(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn);
int HTTP2_Frame_Reset_Stream_Process(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn);
int HTTP2_Frame_Ping_Process(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn);
int HTTP2_Frame_Process(int worker_id, struct GENERIC_HTTP_CONNECTION *conn);

#endif
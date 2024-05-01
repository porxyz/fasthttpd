#ifndef __request_processor_incl__
#define __request_processor_incl__

#include "http2_core.h"

int HTTP_Request_Set_Error_Page(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id, const int error_code, std::string reason = "");
int HTTP_Request_Process(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id);

int HTTP_Request_Send_Response(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id);

#endif
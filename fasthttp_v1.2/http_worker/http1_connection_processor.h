#ifndef __http1_connection_processor_inc__
#define __http1_connection_processor_inc__

#include "http2_core.h"

int HTTP1_Connection_Read_Incoming_Data(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn);
int HTTP1_Connection_Send_Data(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn);
int HTTP1_Connection_Load_File_Chunk(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn);

void HTTP1_Connection_Init(struct HTTP1_CONNECTION *http_conn);

void HTTP1_Connection_HTTP2_101_Upgrade(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn);
void HTTP1_Connection_HTTP2_Upgrade_Directly(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn);

void HTTP1_Connection_Process(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn);
void HTTP1_Connection_Generate_Response(struct GENERIC_HTTP_CONNECTION *conn);

void HTTP1_Connection_Delete(struct HTTP1_CONNECTION *http_conn);

#endif
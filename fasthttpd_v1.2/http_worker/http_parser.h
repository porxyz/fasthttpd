#ifndef __http_parser_incl__
#define __http_parser_incl__

#include "http_worker.h"

int HTTP_Parse_Method(const std::string &method);
bool HTTP_Parse_Request_First_Line(const std::string &first_line, std::string* URI, int* method, int* http_version, size_t* headers_start_offset);

bool HTTP_Parse_Query(const std::string &query_part, std::unordered_map<std::string, std::string> *query_params, unsigned int max_query_args, bool *query_args_limit_exceeded,
                      bool continue_if_exceeded, int start_offset = 0, int end_offset = -1);

bool HTTP_Parse_Raw_URI(const std::string &raw_URI, std::string *URI, std::unordered_map<std::string, std::string> *URI_query_params, unsigned int max_arg_limit, bool continue_if_exceeded);

bool HTTP_Parse_Request_Headers(const std::string &raw_request, size_t headers_start_offset, std::unordered_map<std::string, std::string> *request_headers, size_t* body_start_offset);

bool HTTP_Decode_Content_Range(const std::string &content_range, int64_t *offset_start, int64_t *offset_stop);
std::string HTTP_Encode_Content_Range(int64_t offset_start, int64_t offset_stop, int64_t file_size);

bool HTTP_Decode_POST_type(struct HTTP_REQUEST *http_request);
bool HTTP_Parse_POST_Body(struct HTTP_REQUEST *http_request, const std::string *recv_buffer, size_t start_offset, unsigned int args_limit, unsigned int files_limit, bool continue_if_exceeded);
int HTTP_Parse_Request_Body(const int worker_id, struct GENERIC_HTTP_CONNECTION *conn, const uint32_t stream_id, struct HTTP_REQUEST *request, struct HTTP_RESPONSE *response);

std::string HTTP_Generate_Set_Cookie_Header(const struct HTTP_COOKIE& cookie);
void HTTP_Parse_Cookie_Header(const std::string& cookie_header, std::unordered_map<std::string,std::string> **cookies);
void HTTP_Init_Cookie(struct HTTP_COOKIE *cookie, const char* name, const char* value);
void HTTP_Init_Cookie(struct HTTP_COOKIE *cookie, const std::string* name, const std::string* value);

#endif

#ifndef _HPACK_API_INC__
#define _HPACK_API_INC__

#include "http2_core.h"
#include <nghttp2/nghttp2.h>

bool HPACK_encode_headers(nghttp2_hd_deflater *hpack_encoder, const struct HTTP_RESPONSE *response, std::string *result);
bool HPACK_decode_headers(nghttp2_hd_inflater *hpack_decoder, const std::string& raw_headers, std::unordered_map<std::string, std::string>* headers);

#endif

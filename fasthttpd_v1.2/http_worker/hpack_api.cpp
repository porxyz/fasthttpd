
#include "hpack_api.h"
#include "http_parser.h"
#include "../helper_functions.h"

#include <memory>
#include <iostream>


bool HPACK_encode_headers(nghttp2_hd_deflater *hpack_encoder, const struct HTTP_RESPONSE *response, std::string *result)
{
	if(!hpack_encoder or !response or !result)
	{
		return false;
	}
	
	//add the special status header
	unsigned int headers_num = response->headers.size() + 1;

	//compute for set-cookie headers
	if(response->COOKIES)
	{
		headers_num += response->COOKIES->size();
	}
	
	std::unique_ptr<nghttp2_nv[]> nva;
  	try
  	{
  		nva = std::unique_ptr<nghttp2_nv[]>(new nghttp2_nv[headers_num]);
  	}
  	catch(...)
  	{
  		return false;
  	}
  	
	std::string status_txt = int2str(response->code);

  	nva[0].name = (uint8_t*)":status";
	nva[0].namelen = 7;
	nva[0].value = (uint8_t*)status_txt.c_str();
	nva[0].valuelen = status_txt.size();
	nva[0].flags = NGHTTP2_NV_FLAG_NONE;

    //convert the headers to the format used by nghttp2 library
	unsigned int header_index = 1;
	for(auto it = response->headers.begin(); it != response->headers.end(); it++)
	{
		nva[header_index].name = (uint8_t*) it->first.c_str();
		nva[header_index].namelen = it->first.size();
		nva[header_index].value = (uint8_t*) it->second.c_str();
		nva[header_index].valuelen = it->second.size();
		nva[header_index].flags = NGHTTP2_NV_FLAG_NONE;
		
		header_index++;
	}

	//add set-cookie headers
	std::vector<std::string> set_cookie_h_val;
	if (response->COOKIES)
	{
		set_cookie_h_val= std::vector<std::string> (response->COOKIES->size());

		for (unsigned int i = 0; i < response->COOKIES->size(); i++)
		{
			set_cookie_h_val[i] = HTTP_Generate_Set_Cookie_Header(response->COOKIES->at(i));

			nva[header_index].name = (uint8_t*)"set-cookie";
			nva[header_index].namelen = 10;
			nva[header_index].value = (uint8_t*) set_cookie_h_val[i].c_str();
			nva[header_index].valuelen = set_cookie_h_val[i].size();
			nva[header_index].flags = NGHTTP2_NV_FLAG_NO_INDEX;

			header_index++;
		}
	}

	size_t buff_len = nghttp2_hd_deflate_bound(hpack_encoder, nva.get(), headers_num);
  	
  	std::unique_ptr<uint8_t[]> buff;
  	try
  	{
  		buff = std::unique_ptr<uint8_t[]>(new uint8_t[buff_len]);
  	}
  	catch(...)
  	{
  		return false;
  	}

	int rv = nghttp2_hd_deflate_hd(hpack_encoder, buff.get(), buff_len, nva.get(), headers_num);
	if (rv < 0) 
	{
    		return false;
	}

    result[0] = std::string((const char*)buff.get(), rv);
         
	return true;
}

bool HPACK_decode_headers(nghttp2_hd_inflater *hpack_decoder, const std::string& raw_headers, std::unordered_map<std::string, std::string>* headers)
{
	if(!hpack_decoder or !headers)
	{
		return true;
	}
	
	uint8_t* input = (uint8_t*)raw_headers.c_str();
	size_t input_len = raw_headers.size();
	
	bool should_stop = false;
	while(!should_stop) 
	{
		nghttp2_nv nv;
    	int inflate_flags = 0;
    	size_t proclen;

    	int result = nghttp2_hd_inflate_hd(hpack_decoder, &nv, &inflate_flags, input, input_len, true);
    	if (result < 0) 
    	{
      		return false;
    	}
    		
    	proclen = (size_t)result;
		input += proclen;
		input_len -= proclen;

		if (inflate_flags & NGHTTP2_HD_INFLATE_EMIT)
		{
			std::string header_name((const char *)nv.name, nv.namelen);
			std::string header_value((const char *)nv.value, nv.valuelen);

			if (header_name == "cookie")
			{
				//append separator in case of multiple cookie headers
            	header_value.append(1, ';');

				headers[0]["cookie"].append(header_value);
			}
			else
			{
				headers[0][header_name] = header_value;
			}

			headers->insert(std::make_pair(std::string((const char *)nv.name, nv.namelen), std::string((const char *)nv.value, nv.valuelen)));
		}

		if (inflate_flags & NGHTTP2_HD_INFLATE_FINAL) 
    	{
      		nghttp2_hd_inflate_end_headers(hpack_decoder);
      		should_stop = true;
    	}

    	if ((inflate_flags & NGHTTP2_HD_INFLATE_EMIT) == 0 && input_len == 0) 
    	{
      		should_stop = true;
    	}
	}
	
	return true;
}

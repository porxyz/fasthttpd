#ifndef _ENDIANNESS_CONVERSIONS__
#define _ENDIANNESS_CONVERSIONS__

// Functions for converting between network (big endian) and the host format

#include <cstdint>

#define IS_HOST_BIG_ENDIAN (((uint16_t*)"a")[0] > 0xFF)

//inverse functions use the same logic
#define endian_conv_ntoh16 endian_conv_hton16
#define endian_conv_ntoh24 endian_conv_hton24
#define endian_conv_ntoh32 endian_conv_hton32

//convert a 16 bit value from host to network
static inline uint16_t endian_conv_hton16(const uint16_t val)
{
	if(IS_HOST_BIG_ENDIAN)
	{
		return val;
	}
	else
	{
		return (val >> 8) | ((val & 0xff) << 8);
	}
}

//convert a 32 bit value from host to network
static inline uint32_t endian_conv_hton32(const uint32_t val)
{
	if(IS_HOST_BIG_ENDIAN)
	{
		return val;
	}
	else
	{
		return ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) | ((val & 0xFF0000) >> 8) | (val >> 24);
	}
}

//read a HTTP2 31 bit value, for example stream id
static inline uint32_t endian_conv_ntoh_http31(const uint32_t val)
{
	if(IS_HOST_BIG_ENDIAN)
	{
		return val & 0xEFFFFFFF;
	}
	else
	{
		return endian_conv_hton32(val & 0xFFFFFFEF);
	}
}

//convert a 24 bit value from host to network
//useful for http2 frame length
static inline uint32_t endian_conv_hton24(const uint32_t val)
{
	if(IS_HOST_BIG_ENDIAN)
	{
		return val;
	}
	else
	{
		return ((val & 0xFF) << 16) | (val & 0xFF00) | (val >> 16);
	}
}

#endif

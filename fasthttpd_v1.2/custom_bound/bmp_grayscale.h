#ifndef _bmp_grayscale_inc_
#define _bmp_grayscale_inc_

#include "../custom_bound.h"

#define IS_CPU_LITTLE_ENDIAN ( ((uint16_t*)"a")[0] < 255 )


static inline uint16_t le2h16(uint16_t n) 
{
	if(IS_CPU_LITTLE_ENDIAN)
		return n;

	return ((n & 0xFF) << 8) | (n >> 8);
}

static inline uint32_t le2h32(uint32_t n) 
{
	if(IS_CPU_LITTLE_ENDIAN)
		return n;

	return (n >> 24) | ( ((n >> 16) & 0xff) << 8) | ( ((n >> 8) & 0xff) << 16) | ( (n & 0xff) << 24);
}


inline static uint8_t CLIP(int X)
{
	if(X > 255)
		return 255;

	else if(X < 0)
		return 0;

	return X;
}

inline static uint8_t compute_grayscale_value(uint8_t R, uint8_t G, uint8_t B)
{
	return CLIP((19595 * R + 38470 * G + 7471 * B ) >> 16);
}

int bmp_grayscale_generator(const struct HTTP_CUSTOM_PAGE_HANDLER_ARGUMENTS& args)
{
	if(!args.request->POST_files)
	{
		return HTTP_CONNECTION_OK;
	}

	if(args.request->POST_files->find("image") == args.request->POST_files->end())
	{
		return HTTP_CONNECTION_OK;
	}

	auto& uploaded_file = args.request->POST_files->at("image");

	if(uploaded_file.empty())
	{
		return HTTP_CONNECTION_OK;
	}

	std::string bmp_content = uploaded_file.begin()->second.data;
	char* image_buffer = (char*)bmp_content.c_str();

	//size of BMP header
	if(bmp_content.size() < 54)
	{
		return HTTP_CONNECTION_OK;
	}

	//a valid BMP file always starts with 'BM'
	if(image_buffer[0] != 'B' && image_buffer[1] != 'M')
	{
		return HTTP_CONNECTION_OK;	
	}

	//offset-ul of the pixel array
	int32_t pixel_offset = le2h32( ((uint32_t*)&image_buffer[10])[0]);
	
	//image resolution
	int32_t image_w = le2h32( ((uint32_t*)&image_buffer[18])[0]);
	int32_t image_h = le2h32( ((uint32_t*)&image_buffer[22])[0]);

	int16_t bits_per_pixel =  le2h16( ((uint16_t*)&image_buffer[28])[0]);
	int32_t bmp_compression = le2h32( ((uint32_t*)&image_buffer[30])[0]);

	//only rgb24 with no compression is supported
	if(bits_per_pixel != 24 or bmp_compression != 0)
	{
		return HTTP_CONNECTION_OK;
	}

	//rows are multiple of 4
	int bytes_per_row = 3 * image_w;
	if(bytes_per_row % 4)
	{
		bytes_per_row += 4 - (bytes_per_row % 4);
	}

	/*
	The image size must be nearly equal to the computed size
	*/
	if( ((bmp_content.size() - pixel_offset) - (bytes_per_row*image_h)) < 0 or ((bmp_content.size() - pixel_offset) - (bytes_per_row*image_h)) > 512)
	{
		return HTTP_CONNECTION_OK;
	}

	image_buffer += pixel_offset;
		
	for(int i=0; i<image_h; i++)
	{
		char* current_pixel_row = image_buffer + (bytes_per_row * i);
		
		for(int j=0; j<image_w; j++)
		{
			uint8_t Y = compute_grayscale_value(current_pixel_row[2],current_pixel_row[1],current_pixel_row[0]);
			
			current_pixel_row[0] = Y;
			current_pixel_row[1] = Y;
			current_pixel_row[2] = Y;
			
			current_pixel_row+=3;
		}
	}

	args.response->headers["content-type"] = "image/bmp";
	echo(bmp_content);

	return HTTP_CONNECTION_OK;
}

#endif


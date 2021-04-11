#include "helper_functions.h"

#include <string>
#include <ctime>
#include <math.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h> 
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <string.h>



unsigned int str2uint(const std::string *str,bool *invalid_chars)
{
	unsigned int result=0;
	
	for(size_t i=0; i<str->size(); i++)
	{
	    if(str[0][i] > '9' or str[0][i] < '0')
	    {
	        if(invalid_chars != NULL)
	            *invalid_chars = true;
	        
	        return result;
	    }
	    
	    result*=10;
	    
	    result+= (str[0][i] - '0');
	}
	
	if(invalid_chars != NULL)
	    *invalid_chars = false;
		
	return result;
}


int check_ip_addr(const std::string *addr,int addr_type)
{
	char binary_addr[16];
	return inet_pton(addr_type,addr->c_str(),(void*)binary_addr);
}


int set_socket_nonblock(int sock) 
{
	int flags = fcntl(sock, F_GETFL, 0);
	if (flags == -1)
		return -1;
		
	flags |= FD_CLOEXEC;
	flags |= O_NONBLOCK;
	
	return fcntl(sock, F_SETFL, flags);
}

std::string int2str(int n,uint8_t base)
{
	const char* base_charset="0123456789abcdef";
	
	if(!n)
		return std::string("0");


	bool sign = false;
	if(n < 0)
	{
		n*=-1;
		sign = true;
	}

	char buffer[40];
	buffer[39] = 0;
	char* current_symbol_p = buffer + 38;
    
	while(n)
	{
		char c = base_charset[n % base];
		n/=base;
		
		*current_symbol_p = c;
		current_symbol_p--;
	}
 
	if(sign)
	{
		*current_symbol_p = '-';
		current_symbol_p--;
	}

	return std::string(current_symbol_p + 1);
}

void str_replace_first(std::string* s,const std::string& target,const std::string* replacement)
{
	size_t target_position = s->find(target);
	if(target_position == std::string::npos)
		return;

	s->erase(target_position,target.size());
	s->insert(target_position,*replacement);
 
}


void str_replace_first(std::string* s,const std::string& target,const char* replacement)
{
	size_t target_position = s->find(target);
	if(target_position == std::string::npos)
		return;

	s->erase(target_position,target.size());
	s->insert(target_position,replacement);
 
}

std::string str_ansi_to_lower(const std::string* s)
{
	std::string r = s[0];
	
	for(size_t i=0; i<s->size(); i++)
	{
		if(s[0][i] > 64 and s[0][i] < 91)
			r[i] = s[0][i] + 32;
	}
	
	return r;
}

std::string str_ansi_to_upper(const std::string* s)
{
	std::string r = s[0];
	for(size_t i=0; i<s->size(); i++)
	{
		if(s[0][i] > 90 and s[0][i] < 123)
			r[i] = s[0][i] - 32;
	}
	
	return r;
}

std::string str_ansi_to_lower(std::string s)
{
	return str_ansi_to_lower(&s);
}

std::string str_ansi_to_upper(std::string s)
{
	return str_ansi_to_upper(&s);
}

std::string get_file_extension(const std::string* filename)
{
	std::string result;
	size_t point_position = filename->find_last_of('.');
	
	if(point_position != std::string::npos)
		result = str_ansi_to_lower(filename->substr(point_position+1));
	
	
	return result;
}

std::string get_MIME_type_by_ext(const std::string* filename)
{
	std::string ext = get_file_extension(filename);

	std::string result;

	if(ext == "aac")
		result = "audio/aac";
		
	else if(ext == "abw")
		result = "application/x-abiword";
		
	else if(ext == "avi")
		result = "video/x-msvideo";
		
	else if(ext == "azw")
		result = "application/vnd.amazon.ebook";
		
	else if(ext == "bmp")
		result = "image/bmp";
		
	else if(ext == "bz")
		result = "application/x-bzip";
	
	else if(ext == "bz2")
		result = "application/x-bzip2";
	
	else if(ext == "csh")
		result = "application/x-csh";
	
	else if(ext == "css")
		result = "text/css";
	
	else if(ext == "csv")
		result = "text/csv";
	
	else if(ext == "doc")
		result = "application/msword";
	
	else if(ext == "docx")
		result = "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
	
	else if(ext == "divx")
		result = "video/x-divx";
	
	else if(ext == "eot")
		result = "application/vnd.ms-fontobject";
	
	else if(ext == "epub")
		result = "application/epub+zip";
	
	else if(ext == "es")
		result = "application/ecmascript";
	
	else if(ext == "gif")
		result = "image/gif";
	
	else if(ext == "htm")
		result = "text/html";
	
	else if(ext == "html")
		result = "text/html";
	
	else if(ext == "ico")
		result = "image/x-icon";
	
	else if(ext == "ics")
		result = "text/calendar";
	
	else if(ext == "jar")
		result = "application/java-archive";
	
	else if(ext == "jpeg")
		result = "image/jpeg";
	
	else if(ext == "jpg")
		result = "image/jpeg";
	
	else if(ext == "js")
		result = "application/javascript";
	
	else if(ext == "json")
		result = "application/json";
	
	else if(ext == "mid")
		result = "audio/midi";
	
	else if(ext == "midi")
		result = "audio/midi";
	
	else if(ext == "mkv")
		result = "video/x-matroska";
	
	else if(ext == "mpeg")
		result = "video/mpeg";
	
	else if(ext == "mpkg")
		result = "application/vnd.apple.installer+xml";
	
	else if(ext == "mp3")
		result = "audio/mpeg";
	
	else if(ext == "mp4")
		result = "video/mp4";
	
	else if(ext == "odp")
		result = "application/vnd.oasis.opendocument.presentation";
	
	else if(ext == "ods")
		result = "application/vnd.oasis.opendocument.spreadsheet";
	
	else if(ext == "odt")
		result = "application/vnd.oasis.opendocument.text";
	
	else if(ext == "oga")
		result = "audio/ogg";
	
	else if(ext == "ogv")
		result = "video/ogg";
	
	else if(ext == "ogx")
		result = "application/ogg";
	
	else if(ext == "otf")
		result = "font/otf";
	
	else if(ext == "png")
		result = "image/png";
	
	else if(ext == "pdf")
		result = "application/pdf";
	
	else if(ext == "ppt")
		result = "application/vnd.ms-powerpoint";
	
	else if(ext == "pptx")
		result = "application/vnd.openxmlformats-officedocument.presentationml.presentation";
	
	else if(ext == "rar")
		result = "application/x-rar-compressed";
	
	else if(ext == "rtf")
		result = "application/rtf";
	
	else if(ext == "sh")
		result = "application/x-sh";
	
	else if(ext == "svg")
		result = "image/svg+xml";
	
	else if(ext == "swf")
		result = "application/x-shockwave-flash";
	
	else if(ext == "tar")
		result = "application/x-tar";
	
	else if(ext == "tif")
		result = "image/tiff";
	
	else if(ext == "tiff")
		result = "image/tiff";
	
	else if(ext == "ts")
		result = "application/typescript";
	
	else if(ext == "ttf")
		result = "font/ttf";
	
	else if(ext == "txt")
		result = "text/plain";
	
	else if(ext == "vsd")
		result = "application/vnd.visio";
	
	else if(ext == "wav")
		result = "audio/wav";
	
	else if(ext == "weba")
		result = "audio/webm";
	
	else if(ext == "webm")
		result = "audio/webm";
	
	else if(ext == "webp")
		result = "image/webp";
	
	else if(ext == "woff")
		result = "font/woff";
	
	else if(ext == "woff2")
		result = "font/woff2";
	
	else if(ext == "xhtml")
		result = "application/xhtml+xml";
	
	else if(ext == "xls")
		result = "application/vnd.ms-excel";
	
	else if(ext == "xlsx")
		result = "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
	
	else if(ext == "xml")
		result = "application/xml";
	
	else if(ext == "xul")
		result = "application/vnd.mozilla.xul+xml";
	
	else if(ext == "zip")
		result = "application/zip";
	
	else if(ext == "3gp")
		result = "video/3gpp";
	
	else if(ext == "3g2")
		result = "video/3gpp2";
	
	else if(ext == "7z")
		result = "application/x-7z-compressed";
	
	else
		result = "application/octet-stream";

	return result;
}


void explode(const std::string* str,std::string separator,std::vector<std::string>* result)
{
	size_t last_position(0),separator_position;
	std::string vector_member;

	while(true)
	{
		separator_position = str->find(separator,last_position);
		if(separator_position == std::string::npos)
		{
			vector_member = str->substr(last_position);
			result->push_back(vector_member);
			break;
		}

		vector_member = str->substr(last_position,separator_position - last_position);
		result->push_back(vector_member);
		last_position = separator_position + 1;

	}

}


std::string url_encode(const std::string* s)
{
	std::string result;

	const char* hex_charset = "0123456789ABCDEF";

	for(size_t i=0; i<s->size(); i++)
	{
		if(s[0][i] >= 'A' and s[0][i] <= 'Z')
			result.append(1,s[0][i]);
			
		else if(s[0][i] >= 'a' and s[0][i] <= 'z')
			result.append(1,s[0][i]);
			
		else if(s[0][i] >= '0' and s[0][i] <= '9')
			result.append(1,s[0][i]);
			
		else if(s[0][i] == '_' or s[0][i] == '-' or s[0][i] == '.' or s[0][i] == '~')
			result.append(1,s[0][i]);

		else
		{
			result.append(1,'%');
			result.append(1,hex_charset[(s[0][i] >> 4 ) & 0x0f]);
			result.append(1,hex_charset[s[0][i] & 0x0f]);
		}

	}

	return result;
}

bool url_decode(const std::string* s,std::string* result)
{

	if(!s or !result)
		return false;
		
	
	const char decoding_hex_charset[256] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,                                                                    
						-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,                                                                        
						-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,0,                                                                         
						1,2,3,4,5,6,7,8,9,-1,-1,-1,-1,-1,-1,-1,                                                                                 
						10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,                                                                        
						-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,                                                                        
						10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,                                                                        
						-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,                                                                        
						-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,                                                                        
						-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,                                                                        
						-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,                                                                        
						-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,                                                                        
						-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,                                                                        
						-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,                                                                        
						-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,                                                                        
						-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
					       };
	

	for(size_t i=0; i<s->size(); i++)
	{
		
		if(s[0][i] == '&' or s[0][i] == '=')
			return false;

		else if(s[0][i] == '%')
		{
			if(i + 2 > s->size())
				return false;

			char high_half_pos = decoding_hex_charset[(unsigned char)s[0][i + 1]]; 
			if(high_half_pos == -1)
				return false;

			char low_half_pos = decoding_hex_charset[(unsigned char)s[0][i + 2]]; 
			if(low_half_pos == -1)
				return false;
			

			char c = (char)( low_half_pos | ( high_half_pos << 4) );
			result->append(1,c);
			i+=2;
		}

		else
			result->append(1,s[0][i]);

	}

	return true;
}


std::string rectify_path(const std::string* path)
{
	if(!path)
        	return std::string();
        
	std::string result;
	std::vector<std::string> path_tree, rectified_path_tree;
	explode(path,"/",&path_tree);


	for(size_t i=0; i < path_tree.size(); i++)
	{
		if(path_tree[i] == "." or path_tree[i].empty())
			continue;
			
		else if(path_tree[i] == "..")
		{
			if(!rectified_path_tree.empty())
				rectified_path_tree.pop_back();
			else
				continue;
		}
				
		else
			rectified_path_tree.push_back(path_tree[i]);
		
	}
	
	path_tree.clear();
	
	

	    
	if(path[0][0] == '/' && !rectified_path_tree.empty())
	    rectified_path_tree[0].insert(0,1,'/');
	
	
	for(size_t i=0; i < rectified_path_tree.size(); i++)
	{
		if(i != 0)
			result.append("/");
		
		result.append(rectified_path_tree[i]);
	}


	return result;
}


int get_file_info(const std::string* filename,uint64_t* file_size,time_t* modified_date,bool* is_folder)
{
	struct stat buffer;
	int status;
	status = stat(filename->c_str(),&buffer);
	if(status == -1)
		return -1;

	file_size[0] = buffer.st_size;
	modified_date[0] = buffer.st_mtime;
	is_folder[0] = S_ISDIR(buffer.st_mode);

	return 0;
}

bool convert_http_date2_ctime(const std::string* http_datetime,time_t* result)
{
	if(http_datetime->size() > 29 or http_datetime->size() < 28)
		return false;

	if(http_datetime[0][http_datetime->size() - 3] != 'G' or http_datetime[0][http_datetime->size() - 2] != 'M' or http_datetime[0][http_datetime->size() - 1] != 'T')
		return false;


	std::vector<std::string> datetime_component;
	explode(http_datetime," ",&datetime_component);

	if(datetime_component.size() != 6)
		return false;

	if(datetime_component[1].size() != 2)
		return false;


	std::string day_str = ((datetime_component[1][0] == '0') ? datetime_component[1].substr(1) : datetime_component[1]);

	bool invalid_number;

	unsigned int day_number = str2uint(&day_str,&invalid_number);
	if(invalid_number)
		return false;

	if(datetime_component[2].size() > 3 or datetime_component[2].size() < 2)
		return false;


	unsigned int month_number;
	if(datetime_component[2] == "Jan")
		month_number = 1;
		
	else if(datetime_component[2] == "Feb") 
		month_number = 2;
		
	else if(datetime_component[2] == "Mar") 
		month_number = 3;
		
	else if(datetime_component[2] == "Apr") 
		month_number = 4;
	
	else if(datetime_component[2] == "May") 
		month_number = 5;
	
	else if(datetime_component[2] == "Jun") 
		month_number = 6;
	
	else if(datetime_component[2] == "Jul") 
		month_number = 7;
	
	else if(datetime_component[2] == "Aug") 
		month_number = 8;
	
	else if(datetime_component[2] == "Sep") 
		month_number = 9;
	
	else if(datetime_component[2] == "Oct") 
		month_number = 10;
	
	else if(datetime_component[2] == "Nov") 
		month_number = 11;
	
	else if(datetime_component[2] == "Dec") 
		month_number = 12;
	
	else
	{

		std::string month_str = ((datetime_component[2][0] == '0') ? datetime_component[2].substr(1) : datetime_component[2]);
		month_number = str2uint(&month_str,&invalid_number);
		
		if(invalid_number)
			return false;
			
		if(month_number > 12)
			return false;

	}

	if(datetime_component[3].size() != 4)
		return false;


	unsigned int year_number = str2uint(&datetime_component[3],&invalid_number);
	if(invalid_number)
		return false;

	if(datetime_component[4].size() != 8)
		return false;


	std::vector<std::string> time_component;
	explode(&datetime_component[4],":",&time_component);

	if(time_component.size() != 3)
		return false;

	if(time_component[0].size() != 2 or time_component[1].size() != 2 or time_component[2].size() != 2)
		return false;

	std::string hour_str = ((time_component[0][0] == '0') ? time_component[0].substr(1) : time_component[0]);
	unsigned int hour_number = str2uint(&hour_str,&invalid_number);
	if(invalid_number)
		return false;


	std::string minute_str = ((time_component[1][0] == '0') ? time_component[1].substr(1) : time_component[1]);
	unsigned int minute_number = str2uint(&minute_str,&invalid_number);
	if(invalid_number)
		return false;


	std::string second_str = ((time_component[2][0] == '0') ? time_component[2].substr(1) : time_component[2]);
	unsigned int second_number = str2uint(&second_str,&invalid_number);
	if(invalid_number)
		return false;


	struct tm time_struct;
	memset(&time_struct,0,sizeof(struct tm));

	time_struct.tm_sec = second_number;
	time_struct.tm_min = minute_number;
	time_struct.tm_hour = hour_number;
	time_struct.tm_mday = day_number;
	time_struct.tm_mon = month_number - 1;
	time_struct.tm_year = year_number - 1900;

	result[0] = timegm(&time_struct);

	return true;    
}


std::string convert_ctime2_http_date(time_t t)
{
	struct tm* time_struct = gmtime(&t);
	char buffer[30];
	std::string result;

	strftime(buffer,30,"%a, %d %b %Y %H:%M:%S ",time_struct);
	result = buffer;
	result.append("GMT");

	return result;
}





std::string html_special_chars_escape(const char* s)
{
	std::string result;

	for(size_t i=0; i<strlen(s); i++)
	{

		if(s[i] == '<')
			result.append("&lt;");
			
		else if(s[i] == '>')
			result.append("&gt;");
			
		else if(s[i] == '"')
			result.append("&quot;");
			
		else if(s[i] == '\'')
			result.append("&#039;");
			
		else if(s[i] == '&')
			result.append("&amp;");
			
		else
			result.append(1,s[i]);

	}

	return result;
}


std::string html_special_chars_escape(const std::string* s)
{
	if(!s)
		return std::string();

		
	return html_special_chars_escape(s->c_str());
}

std::string get_OS_name()
{
	struct utsname system_info;
	if(uname(&system_info) == -1)
		return std::string("Generic Linux");
		
		
	return std::string(system_info.sysname);
}

std::string get_OS_version()
{
	struct utsname system_info;
	if(uname(&system_info) == -1)
		return std::string("undefined version");
		
		
	return std::string(system_info.release);
}






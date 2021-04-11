#ifndef __helper_functions_incl__
#define __helper_functions_incl__  

#include <string>
#include <vector>
#include <ctime>
#include <cstdint>

unsigned int str2uint(const std::string *str,bool *invalid_chars = NULL);
int check_ip_addr(const std::string *addr,int addr_type);
int set_socket_nonblock(int sock);
std::string int2str(int n,uint8_t base = 10);
void str_replace_first(std::string* s,const std::string& target,const std::string* replacement);
void str_replace_first(std::string* s,const std::string& target,const char* replacement);
std::string str_ansi_to_lower(const std::string* s);
std::string str_ansi_to_upper(const std::string* s);
std::string str_ansi_to_lower(std::string s);
std::string str_ansi_to_upper(std::string s);
std::string get_file_extension(const std::string* filename);
std::string get_MIME_type_by_ext(const std::string* filename);
void explode(const std::string* str,std::string separator,std::vector<std::string>* result);
std::string url_encode(const std::string* s);
bool url_decode(const std::string* s,std::string* result);
std::string rectify_path(const std::string* path);
int get_file_info(const std::string* filename,uint64_t* file_size,time_t* last_modified,bool* is_folder);
bool convert_http_date2_ctime(const std::string* http_datetime,time_t* result);
std::string convert_ctime2_http_date(time_t t);
std::string html_special_chars_escape(const std::string* s);
std::string html_special_chars_escape(const char* s);
std::string get_OS_name();
std::string get_OS_version();


#endif

#ifndef __server_config_incl__
#define __server_config_incl__

#include <string>
#include <unordered_map>

#define MAX_CONFIG_FILE_SIZE 2 * 1024 * 1024

#define DEFAULT_CONFIG_SERVER_NAME "fasthttpd"
#define DEFAULT_CONFIG_SERVER_VERSION "1.2"
#define DEFAULT_CONFIG_SERVER_SHUTDOWN_TIMEOUT "8"

#define DEFAULT_CONFIG_SERVER_HTTP_PORT "80"
#define DEFAULT_CONFIG_SERVER_HTTPS_PORT "443"

#define DEFAULT_CONFIG_SERVER_IP_VERSION "6"
#define DEFAULT_CONFIG_SERVER_IPV6_DUAL_STACK "true"

#define DEFAULT_CONFIG_SERVER_IP_ADDR "0.0.0.0"
#define DEFAULT_CONFIG_SERVER_IP_ADDR6 "::"

#define DEFAULT_CONFIG_SERVER_MAXCONN "128" 
#define DEFAULT_CONFIG_SERVER_READ_BUFFER_SIZE "65"
#define DEFAULT_CONFIG_SERVER_REQ_TIMEOUT "30" 
#define DEFAULT_CONFIG_SERVER_WORKERS "50"
#define DEFAULT_CONFIG_SERVER_LISTENERS "1"
#define DEFAULT_CONFIG_SERVER_LOAD_BALANCER_ALGO "rr" 
#define DEFAULT_CONFIG_SERVER_MAX_REQ_SIZE "1024"
#define DEFAULT_CONFIG_SERVER_MAX_UPLOAD_FILES "32"
#define DEFAULT_CONFIG_SERVER_MAX_POST_ARGS "64"
#define DEFAULT_CONFIG_SERVER_MAX_QUERY_ARGS "32"
#define DEFAULT_CONFIG_SERVER_MAX_FILE_ACCESS_CACHE_SIZE "64"

#define DEFAULT_CONFIG_HTTP2_MAX_FRAME_SIZE "16"
#define DEFAULT_CONFIG_HTTP2_INIT_WINDOW_SIZE "65"
#define DEFAULT_CONFIG_HTTP2_MAX_CONCURRENT_STREAMS "100"

#define DEFAULT_CONFIG_SERVER_ERROR_PAGE_FOLDER "config/error_pages"

#define DEFAULT_CONFIG_SERVER_HOSTLIST_FILE "config/host_list.conf"
#define DEFAULT_CONFIG_SERVER_HOSTNAME "localhost"
#define DEFAULT_CONFIG_SERVER_HOSTNAME_PATH "www"

#define DEFAULT_CONFIG_SERVER_DIR_LISTING_TEMPLATE "config/directory_listing_template.html"


#ifndef NO_MOD_MYSQL
#define DEFAULT_CONFIG_SERVER_MYSQL_HOSTNAME "localhost"
#define DEFAULT_CONFIG_SERVER_MYSQL_USERNAME "root"
#define DEFAULT_CONFIG_SERVER_MYSQL_PASSWORD ""
#define DEFAULT_CONFIG_SERVER_MYSQL_PORT "3306"
#endif


extern std::unordered_map<std::string, std::string > SERVER_CONFIGURATION;
extern std::unordered_map<std::string, std::string > SERVER_HOSTNAMES;
extern std::unordered_map<int, std::string > SERVER_ERROR_PAGES;
extern std::string SERVER_DIRECTORY_LISTING_TEMPLATE;
extern bool is_server_load_balancer_fair;

void load_server_config(char* config_file = NULL);

bool server_config_variable_exists(const char* key);

bool server_error_page_exists(int error_code);

bool is_server_config_variable_true(const char* key);
bool is_server_config_variable_false(const char* key);

bool http_host_exists(const std::string* key);

void parse_config_line(const std::string *config_line,std::unordered_map<std::string,std::string>* config_map);

#endif

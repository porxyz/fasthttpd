#include <string>
#include <unordered_map>
#include <fstream>
#include <cstdlib> 
#include <errno.h>
#include <cstring>
#include <cstdlib>
#include <sys/types.h>
#include <sys/socket.h>


#include "server_config.h"
#include "helper_functions.h"
#include "server_journal.h"


std::unordered_map<std::string , std::string > SERVER_CONFIGURATION;
std::unordered_map<std::string , std::string > SERVER_HOSTNAMES;
std::unordered_map<int , std::string > SERVER_ERROR_PAGES;

void parse_config_line(const std::string *config_line,std::unordered_map<std::string,std::string>* config_map)
{
	size_t separator_position = config_line->find('=');
	if(separator_position == std::string::npos)
		return;


	std::string key,value;

	key=config_line->substr(0,separator_position);
	value=config_line->substr(separator_position+1,std::string::npos);

	if(key.find_first_not_of(' ') <= key.size())
		key = key.substr(key.find_first_not_of(' '), std::string::npos);
		
		
	key=key.substr(0,key.find_last_not_of(' ')+1);

	if(value.find_first_not_of(' ') <= value.size())
		value=value.substr(value.find_first_not_of(' '),std::string::npos);
		
		
	value=value.substr(0,value.find_last_not_of(' ')+1);

	if(!key.empty() and !value.empty() and key[0]!='#')
		config_map->insert(std::pair<std::string,std::string>(key,value));
		
}

bool server_config_variable_exists(const char* key)
{
    if(SERVER_CONFIGURATION.find(key) != SERVER_CONFIGURATION.end()) 
    	return true;
    	
    return false;
}

bool http_host_exists(const std::string* key)
{
    if(SERVER_HOSTNAMES.find(key[0]) != SERVER_HOSTNAMES.end()) 
    	return true;
    	
    return false;
}

bool server_error_page_exists(int error_code)
{
    if(SERVER_ERROR_PAGES.find(error_code) != SERVER_ERROR_PAGES.end()) 
    	return true;
    	
    return false;
}

bool is_server_config_variable_true(const char* key)
{
	auto it = SERVER_CONFIGURATION.find(key);
	
	if(it == SERVER_CONFIGURATION.end())
		return false;
	
		
	if(it->second == "1" or it->second == "true" or it->second == "TRUE")
		return true;
		
		
	return false;
}

bool is_server_config_variable_false(const char* key)
{
	auto it = SERVER_CONFIGURATION.find(key);
	
	if(it == SERVER_CONFIGURATION.end())
		return false;
	
		
	if(it->second == "0" or it->second == "false" or it->second == "FALSE")
		return true;
		
		
	return false;
}


void check_server_config_uintval(const char* key,const char* default_value, unsigned int min_val, uint64_t max_val, const std::vector<unsigned int>* exact_values = NULL)
{
	if(!server_config_variable_exists(key))
	{
		SERVER_CONFIGURATION[key] = default_value;
		
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING)); 
		SERVER_JOURNAL_WRITE(" No value specified for ");
		SERVER_JOURNAL_WRITE(key);
		SERVER_JOURNAL_WRITE("!\nLoading default: ");
		SERVER_JOURNAL_WRITE(default_value);
		SERVER_JOURNAL_WRITE("\n\n");
	}
	else
	{
		bool is_bad_num;
		unsigned int num_val = str2uint(&SERVER_CONFIGURATION[key],&is_bad_num);
		
		if(!is_bad_num)
		{
			if(exact_values)
			{
				is_bad_num = true;
				unsigned int* raw_values = (unsigned int*)exact_values->data();
				for(unsigned int i=0; i<exact_values->size(); i++)
				{
					if(raw_values[i] == num_val)
					{
						is_bad_num = false;
						break;
					}
				}
			}
			
			else if(num_val > max_val or num_val < min_val)
				is_bad_num = true;
			
		}
		
		if(is_bad_num)
		{
			SERVER_CONFIGURATION[key] = default_value;
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
			SERVER_JOURNAL_WRITE(" Invalid value specified for ",true);
			SERVER_JOURNAL_WRITE(key,true);
			SERVER_JOURNAL_WRITE("!\nLoading default: ",true);
			SERVER_JOURNAL_WRITE(default_value,true);
			SERVER_JOURNAL_WRITE("\nPlease consult the server manual for more information!",true);
			SERVER_JOURNAL_WRITE("\n\n",true);
		}
	}
}


void load_server_error_pages()
{
	char read_buffer[1024 * 10];
	
	int error_page_codes[] = {400,401,402,403,404,405,406,407,408,409,410,411,412,413,414,415,416,417,418,422,428,429,431,451,500,501,502,503,504,505,511,520,522,524,0};

	int i=0;
	while(error_page_codes[i] != 0)
	{
		std::string error_page_path = SERVER_CONFIGURATION["error_page_folder"];
		error_page_path.append("/error_");
		error_page_path.append(int2str(error_page_codes[i]));
		error_page_path.append(".html");
		
		i++;

		std::ifstream error_page_stream;
		error_page_stream.open(error_page_path.c_str());
		if(!error_page_stream.is_open())
		{
			error_page_stream.close();
			continue;
		}

		error_page_stream.seekg(0, std::ios::end);
		size_t bytes_to_read = error_page_stream.tellg();
		error_page_stream.seekg(0, std::ios::beg);

		//to be modified ; //load from config
		if(bytes_to_read > 10240)
			bytes_to_read = 10240;

		error_page_stream.read(read_buffer,bytes_to_read);
		error_page_stream.close();

		SERVER_ERROR_PAGES[error_page_codes[i - 1]] = std::move(std::string(read_buffer,bytes_to_read));

		str_replace_first(&SERVER_ERROR_PAGES[error_page_codes[i - 1]],"$SERVERNAME",&SERVER_CONFIGURATION["server_name"]);
		str_replace_first(&SERVER_ERROR_PAGES[error_page_codes[i - 1]],"$SERVERVERSION",DEFAULT_CONFIG_SERVER_VERSION);
	}

	if(!server_error_page_exists(500))
	{
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
		SERVER_JOURNAL_WRITE(" No server error page for status 500 provided!",true);
		SERVER_JOURNAL_WRITE("\n\n",true);
		exit(-1);
	}

}


void load_hosts_list()
{
	std::ifstream hosts_file_stream;
	hosts_file_stream.open(SERVER_CONFIGURATION["host_list_file"].c_str(),std::ios::ate);
	
	if(!hosts_file_stream.is_open())
	{
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
                SERVER_JOURNAL_WRITE(" Unable to open the hosts list file ",true);
		SERVER_JOURNAL_WRITE(SERVER_CONFIGURATION["host_list_file"],true);
		SERVER_JOURNAL_WRITE("\nError code: 0x",true);
		SERVER_JOURNAL_WRITE(std::hex,true);
		SERVER_JOURNAL_WRITE(errno,true);
		SERVER_JOURNAL_WRITE("\n",true);
		SERVER_JOURNAL_WRITE(strerror(errno),true);
		SERVER_JOURNAL_WRITE("\n\n",true);
		SERVER_JOURNAL_WRITE(std::dec,true);
	}

	else
	{
		size_t hosts_file_size = hosts_file_stream.tellg();
		hosts_file_stream.clear();
		hosts_file_stream.seekg(0, std::ios::beg);

		if(hosts_file_size > MAX_CONFIG_FILE_SIZE)
		{
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
			SERVER_JOURNAL_WRITE(" The hosts list file is too large!\nHosts list file size: ",true);
			SERVER_JOURNAL_WRITE(hosts_file_size,true);
			SERVER_JOURNAL_WRITE(" bytes",true);
			SERVER_JOURNAL_WRITE("\n\n",true);
			exit(-1);
		}

		std::string current_line;
		
		while(std::getline(hosts_file_stream,current_line))
			parse_config_line(&current_line,&SERVER_HOSTNAMES);


	}
	
	hosts_file_stream.close();

	if(SERVER_HOSTNAMES.size() == 0)
	{
		SERVER_HOSTNAMES[DEFAULT_CONFIG_SERVER_HOSTNAME] = DEFAULT_CONFIG_SERVER_HOSTNAME_PATH;
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING));
		SERVER_JOURNAL_WRITE(" No host defined!\nLoading default: ");
		SERVER_JOURNAL_WRITE(DEFAULT_CONFIG_SERVER_HOSTNAME);
		SERVER_JOURNAL_WRITE(" => ");
		SERVER_JOURNAL_WRITE(DEFAULT_CONFIG_SERVER_HOSTNAME_PATH);
		SERVER_JOURNAL_WRITE("\n\n");
	}

	if(!server_config_variable_exists("default_host"))
	{
		SERVER_CONFIGURATION["default_host"] = (SERVER_HOSTNAMES.begin())->first; 
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING)); 
		SERVER_JOURNAL_WRITE(" No default host specified!\nLoading default: ");
		SERVER_JOURNAL_WRITE(SERVER_CONFIGURATION["default_host"]);
		SERVER_JOURNAL_WRITE("\n\n");
	}
	else
	{

		if(!http_host_exists(&SERVER_CONFIGURATION["default_host"]))
		{
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
			SERVER_JOURNAL_WRITE(" Default host ",true);
			SERVER_JOURNAL_WRITE(SERVER_CONFIGURATION["default_host"],true);
			SERVER_JOURNAL_WRITE(" does not match any defined hosts!\n\n",true);
			exit(-1);
		}

	}

}




void load_server_config(char* config_file)
{
	if(config_file == NULL)
	{
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING));
		SERVER_JOURNAL_WRITE(" No configuration file provided! Loading defaults!\n\n");
	}

	else
	{
		std::ifstream config_file_stream;
		config_file_stream.open(config_file,std::ios::ate);
		
		if(!config_file_stream.is_open())
		{
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
			SERVER_JOURNAL_WRITE(" Unable to open configuration file ",true);
			SERVER_JOURNAL_WRITE(config_file,true);
			SERVER_JOURNAL_WRITE("\nError code: 0x",true);
			SERVER_JOURNAL_WRITE(std::hex,true);
			SERVER_JOURNAL_WRITE(errno,true);
			SERVER_JOURNAL_WRITE("\n",true);
			SERVER_JOURNAL_WRITE(strerror(errno),true);
			SERVER_JOURNAL_WRITE("\n\n",true);
			SERVER_JOURNAL_WRITE(std::dec,true);
			exit(-1);
		}

		size_t config_file_size = config_file_stream.tellg();
		config_file_stream.clear();
		config_file_stream.seekg(0, std::ios::beg);

		if(config_file_size > MAX_CONFIG_FILE_SIZE)
		{
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true);
			SERVER_JOURNAL_WRITE(" The configuration file is too large!\nConfiguration file size: ",true);
			SERVER_JOURNAL_WRITE(config_file_size,true); SERVER_JOURNAL_WRITE(" bytes",true);
			SERVER_JOURNAL_WRITE("\n\n",true);
			exit(-1);
		}

		std::string current_line;
		while(std::getline(config_file_stream,current_line))
			parse_config_line(&current_line,&SERVER_CONFIGURATION);


		config_file_stream.close(); 
	}


	if(server_config_variable_exists("journal_normal_output_file") && server_config_variable_exists("journal_error_output_file"))
		SERVER_JOURNAL_INIT(SERVER_CONFIGURATION["journal_normal_output_file"].c_str(),SERVER_CONFIGURATION["journal_error_output_file"].c_str());

	else if(server_config_variable_exists("journal_normal_output_file") && !server_config_variable_exists("journal_error_output_file"))
		SERVER_JOURNAL_INIT(SERVER_CONFIGURATION["journal_normal_output_file"].c_str(),NULL);

	else if(!server_config_variable_exists("journal_normal_output_file") && server_config_variable_exists("journal_error_output_file"))
		SERVER_JOURNAL_INIT(NULL,SERVER_CONFIGURATION["journal_error_output_file"].c_str());



	if(is_server_config_variable_true("disable_journal"))
		SERVER_JOURNAL_DISABLED = true;
	

	if(is_server_config_variable_true("journal_localtime_reporting"))
		SERVER_JOURNAL_LOCALTIME_REPORTING=true;




	//sanity check 

 		
	std::vector<unsigned int> allowed_ip_version = {4,6};
	check_server_config_uintval("ip_version",DEFAULT_CONFIG_SERVER_IP_VERSION,0,0,&allowed_ip_version);
	
	check_server_config_uintval("listen_http_port",DEFAULT_CONFIG_SERVER_HTTP_PORT,1,65534);
		
	check_server_config_uintval("max_connections",DEFAULT_CONFIG_SERVER_MAXCONN,1,1 << 28);
	
	check_server_config_uintval("max_uploaded_files",DEFAULT_CONFIG_SERVER_MAX_UPLOAD_FILES,1,1 << 14);
	
	check_server_config_uintval("max_post_args",DEFAULT_CONFIG_SERVER_MAX_POST_ARGS,1,1 << 14);
	
	check_server_config_uintval("max_query_args",DEFAULT_CONFIG_SERVER_MAX_QUERY_ARGS,1,1 << 14);
	
	check_server_config_uintval("shutdown_wait_timeout",DEFAULT_CONFIG_SERVER_SHUTDOWN_TIMEOUT,2,20);
	
	check_server_config_uintval("request_timeout",DEFAULT_CONFIG_SERVER_REQ_TIMEOUT,0,600);
	
	check_server_config_uintval("read_buffer_size",DEFAULT_CONFIG_SERVER_READ_BUFFER_SIZE,1024,1 << 28);

	check_server_config_uintval("server_workers",DEFAULT_CONFIG_SERVER_WORKERS,1,1 << 14);
	
	check_server_config_uintval("max_request_size",DEFAULT_CONFIG_SERVER_MAX_REQ_SIZE,1024,uint64_t(1) << 34);



	if(!server_config_variable_exists("ip_addr"))
	{
		SERVER_CONFIGURATION["ip_addr"] = (SERVER_CONFIGURATION["ip_version"] == "4") ? DEFAULT_CONFIG_SERVER_IP_ADDR : DEFAULT_CONFIG_SERVER_IP_ADDR6; 
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING)); 
		SERVER_JOURNAL_WRITE(" No listening ip address specified!\nLoading default: ");
		SERVER_JOURNAL_WRITE(SERVER_CONFIGURATION["ip_addr"]);
		SERVER_JOURNAL_WRITE("\n\n");
	}
	else
	{
		int r = check_ip_addr(&SERVER_CONFIGURATION["ip_addr"],((SERVER_CONFIGURATION["ip_version"] == std::string("4")) ? AF_INET : AF_INET6));
		if(r == 0)
		{
			SERVER_CONFIGURATION["ip_addr"] = (SERVER_CONFIGURATION["ip_version"] == std::string("4")) ? DEFAULT_CONFIG_SERVER_IP_ADDR : DEFAULT_CONFIG_SERVER_IP_ADDR6;
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
			SERVER_JOURNAL_WRITE(" Bad listening ip address specified!\nLoading default: ",true);
			SERVER_JOURNAL_WRITE(SERVER_CONFIGURATION["ip_addr"],true);
			SERVER_JOURNAL_WRITE("\n\n",true);
		}

		else if(r == -1)
		{
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
			SERVER_JOURNAL_WRITE(" Unable to test ip address ",true);
			SERVER_JOURNAL_WRITE(SERVER_CONFIGURATION["ip_addr"],true);
			SERVER_JOURNAL_WRITE("\nError code: 0x",true);
			SERVER_JOURNAL_WRITE(std::hex,true);
			SERVER_JOURNAL_WRITE(errno,true);
			SERVER_JOURNAL_WRITE("\n",true);
			SERVER_JOURNAL_WRITE(strerror(errno),true);
			SERVER_JOURNAL_WRITE("\n\n",true);
			SERVER_JOURNAL_WRITE(std::dec,true);
			exit(-1);
		}

	}


	SERVER_CONFIGURATION["server_version"] = DEFAULT_CONFIG_SERVER_VERSION;

	if(!server_config_variable_exists("server_name"))
	{
		SERVER_CONFIGURATION["server_name"] = DEFAULT_CONFIG_SERVER_NAME;
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING)); 
		SERVER_JOURNAL_WRITE(" No server name specified!\nLoading default: ");
		SERVER_JOURNAL_WRITE(SERVER_CONFIGURATION["server_name"]);
		SERVER_JOURNAL_WRITE("\n\n");
	}

	if(is_server_config_variable_true("enable_https"))
	{
		check_server_config_uintval("listen_https_port",DEFAULT_CONFIG_SERVER_HTTPS_PORT,1,65534);

		if(!server_config_variable_exists("ssl_cert_file"))
		{
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
			SERVER_JOURNAL_WRITE(" HTTPS is enabled but no certificate file is provided!\n\n",true);
			exit(-1);
		}

		if(!server_config_variable_exists("ssl_key_file"))
		{
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
			SERVER_JOURNAL_WRITE(" HTTPS is enabled but no private key file is provided!\n\n",true);
			exit(-1);
		}

	}	


	SERVER_CONFIGURATION["os_name"] = get_OS_name();
	SERVER_CONFIGURATION["os_version"] = get_OS_version();


	if(!server_config_variable_exists("host_list_file"))
	{
		SERVER_CONFIGURATION["host_list_file"] = DEFAULT_CONFIG_SERVER_HOSTLIST_FILE;
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING)); 
		SERVER_JOURNAL_WRITE(" No hosts list file specified!\nLoading default: ");
		SERVER_JOURNAL_WRITE(SERVER_CONFIGURATION["host_list_file"]);
		SERVER_JOURNAL_WRITE("\n\n");
	}


	load_hosts_list();

	if(!server_config_variable_exists("error_page_folder"))
	{
		SERVER_CONFIGURATION["error_page_folder"] = DEFAULT_CONFIG_SERVER_ERROR_PAGE_FOLDER;
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING)); 
		SERVER_JOURNAL_WRITE(" No server error page folder specified!\nLoading default: ");
		SERVER_JOURNAL_WRITE(SERVER_CONFIGURATION["error_page_folder"]);
		SERVER_JOURNAL_WRITE("\n\n");
	}


	load_server_error_pages();



	if(is_server_config_variable_true("enable_MOD_MYSQL"))
	{
	#ifdef NO_MOD_MYSQL 
		SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
		SERVER_JOURNAL_WRITE(" MOD_MYSQL is enabled but the MYSQL support module is not compiled!\n\n",true);
	#else

		if(!server_config_variable_exists("mysql_hostname"))
		{
			SERVER_CONFIGURATION["mysql_hostname"] = DEFAULT_CONFIG_SERVER_MYSQL_HOSTNAME;
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING)); 
			SERVER_JOURNAL_WRITE(" MOD_MYSQL is enabled but no mysql hostname is specified!\nLoading default: ");
			SERVER_JOURNAL_WRITE(SERVER_CONFIGURATION["mysql_hostname"]);
			SERVER_JOURNAL_WRITE("\n\n");
		}


		if(!server_config_variable_exists("mysql_username"))
		{
			SERVER_CONFIGURATION["mysql_username"] = DEFAULT_CONFIG_SERVER_MYSQL_USERNAME;
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING)); 
			SERVER_JOURNAL_WRITE(" MOD_MYSQL is enabled but no mysql username is specified!\nLoading default: ");
			SERVER_JOURNAL_WRITE(SERVER_CONFIGURATION["mysql_username"]);
			SERVER_JOURNAL_WRITE("\n\n");
		}


		if(!server_config_variable_exists("mysql_password"))
		{
			SERVER_CONFIGURATION["mysql_password"] = DEFAULT_CONFIG_SERVER_MYSQL_PASSWORD;
			SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING)); 
			SERVER_JOURNAL_WRITE(" MOD_MYSQL is enabled but no mysql password is specified!\nLoading default: ");
			SERVER_JOURNAL_WRITE(SERVER_CONFIGURATION["mysql_password"]);
			SERVER_JOURNAL_WRITE("\n\n");
		}

		bool is_bad_num;
		if(server_config_variable_exists("mysql_port"))
		{
			unsigned int mysql_port = str2uint(&SERVER_CONFIGURATION["mysql_port"],&is_bad_num);

			if(is_bad_num or mysql_port == 0 or mysql_port > 65534)
			{
				SERVER_CONFIGURATION["mysql_port"] = DEFAULT_CONFIG_SERVER_MYSQL_PORT;
				SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
				SERVER_JOURNAL_WRITE(" MOD_MYSQL: Bad mysql port specified!\nLoading default: ",true); 
				SERVER_JOURNAL_WRITE(SERVER_CONFIGURATION["mysql_port"],true);
				SERVER_JOURNAL_WRITE("\n\n",true);
			}
		}


		if(server_config_variable_exists("mysql_connection_timeout"))
		{
			unsigned int mysql_timeout = str2uint(&SERVER_CONFIGURATION["mysql_connection_timeout"],&is_bad_num);

			if(is_bad_num or mysql_timeout > 60)
			{
				SERVER_CONFIGURATION.erase("mysql_connection_timeout");
				SERVER_JOURNAL_WRITE(journal_strtime(SERVER_JOURNAL_LOCALTIME_REPORTING),true); 
				SERVER_JOURNAL_WRITE(" MOD_MYSQL: Bad mysql connection timeout specified!\nLoading default: NULL\n\n",true); 
			}
		}


	#endif
	}

}






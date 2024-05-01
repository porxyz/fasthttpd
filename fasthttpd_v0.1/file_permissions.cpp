#include <string>
#include <vector>
#include <unordered_map>
#include <list>
#include <fstream>
#include <regex>


#include "helper_functions.h"
#include "server_config.h"
#include "file_permissions.h"

struct access_descriptor 
{
	bool defined;
	bool order_deny;
	bool fake_404;
	std::vector<std::regex> entry_list;
};

struct access_descriptor_cache_entry
{
	struct access_descriptor descriptor;
	std::list<std::string>::iterator list_iterator;
};

bool disable_access_control_API;
size_t max_AD_cache_size;

std::vector<std::unordered_map<std::string, struct access_descriptor_cache_entry>> access_descriptor_cache;
std::vector<std::list<std::string>> access_descriptor_list;


static inline bool is_AD_in_cache(size_t worker_id, const std::string& path)
{
	bool found = false;
	
	auto it = access_descriptor_cache[worker_id].find(path);
	if(it != access_descriptor_cache[worker_id].end())
	{
		found = true;
		
		//perform LRU cache update
		struct access_descriptor_cache_entry cache_entry = it->second;
		
		access_descriptor_list[worker_id].erase(cache_entry.list_iterator);
		access_descriptor_list[worker_id].push_front(path);
		
		cache_entry.list_iterator = access_descriptor_list[worker_id].begin();
		
		access_descriptor_cache[worker_id][path] = cache_entry;
	}

	return found;
}

//remove last entry in the LRU cache
static inline void remove_last_used_AD(size_t worker_id)
{
	if(access_descriptor_list[worker_id].size() <= max_AD_cache_size)
		return;
	
	std::string last_used = access_descriptor_list[worker_id].back();
	access_descriptor_cache[worker_id].erase(last_used);
	access_descriptor_list[worker_id].pop_back();
}

static inline bool pattern_compare(const std::string& needle,const std::regex& haystack)
{
	return std::regex_match(needle,haystack);
}

void read_access_config_file(size_t worker_id, const std::string& path)
{
	struct access_descriptor_cache_entry cache_entry;
	
	std::string access_configuration_file_path = path;
	access_configuration_file_path.append("/.access_config");

	std::ifstream access_configuration_file;
	access_configuration_file.open(access_configuration_file_path.c_str());

	if(!access_configuration_file.is_open())
	{
		cache_entry.descriptor.defined = false;
		
		access_configuration_file.close();
		
		access_descriptor_list[worker_id].push_front(path);
		cache_entry.list_iterator = access_descriptor_list[worker_id].begin();
		access_descriptor_cache[worker_id][path] = cache_entry;
		remove_last_used_AD(worker_id);
		
		return;
	}

	cache_entry.descriptor.defined = true;

	std::string current_line;
	std::unordered_map <std::string , std::string> config_params_map;

	std::regex escape("[.^$|()\\[\\]{}*+?\\\\]");
	std::string rep("\\$&");

	std::string escaped_haystack;

	while(std::getline(access_configuration_file,current_line))
	{
		if(current_line[0] == '#')
			continue;

		if(current_line.find_first_not_of(' ') == std::string::npos or current_line.size() == 0)
			continue;

		if(current_line.find('=') != std::string::npos)
		{
			parse_config_line(&current_line,&config_params_map);
			continue;
		}

		escaped_haystack = std::regex_replace(current_line,escape,rep);

		while(true)
		{
			size_t target_position = escaped_haystack.find("\\*");
			if(target_position == std::string::npos)
				break;


			escaped_haystack.erase(target_position,2);
			escaped_haystack.insert(target_position,".*");
		}

		cache_entry.descriptor.entry_list.push_back(std::regex(escaped_haystack));
	}

	access_configuration_file.close();


	if(config_params_map.find(std::string("order")) != config_params_map.end())
	{
		if(config_params_map["order"] == std::string("allow"))
			cache_entry.descriptor.order_deny = false;
			
		else
			cache_entry.descriptor.order_deny = true;
			
	}
	else
		cache_entry.descriptor.order_deny = true;


	if(config_params_map.find(std::string("fake_404")) != config_params_map.end())
	{
		if(config_params_map["fake_404"] == std::string("true") or config_params_map["fake_404"] == std::string("1"))
			cache_entry.descriptor.fake_404 = true;
			
		else
			cache_entry.descriptor.fake_404 = false;
	}
	else
		cache_entry.descriptor.fake_404 = false;

	access_descriptor_list[worker_id].push_front(path);
	cache_entry.list_iterator = access_descriptor_list[worker_id].begin();
	access_descriptor_cache[worker_id][path] = cache_entry;
	remove_last_used_AD(worker_id);
}


static inline int check_with_descriptor(const std::string& filename,struct access_descriptor& desc)
{
	if(desc.defined)
	{
		bool match = false;
		
		for(size_t i = 0; i<desc.entry_list.size(); i++)
		{
			if(pattern_compare(filename,desc.entry_list[i]))
			{
				match = true;
				break;
			}
		}

		if(match and desc.order_deny)
			return (desc.fake_404) ? 404 : 403;
					
		if(!match and !desc.order_deny)
			return (desc.fake_404) ? 404 : 403;
	}
			
	return 0;
}

int check_file_access(size_t worker_id,const std::string& filename,const std::string& host_path)
{
	if(disable_access_control_API)
		return 0;
	
	std::vector<std::string> path_tree;
	explode(&filename,"/",&path_tree);
	
	std::string current_path = host_path;
	if(host_path[host_path.size() - 1] == '/')
		current_path.pop_back();
		
	for(size_t i=0; i<path_tree.size(); i++)
	{
		if(path_tree[i].empty())
			continue;
			
		if(!is_AD_in_cache(worker_id,current_path))
			read_access_config_file(worker_id,current_path);
			
		int check_val = check_with_descriptor(path_tree[i],access_descriptor_cache[worker_id][current_path].descriptor);
		if(check_val != 0)
			return check_val;
			
		current_path.append(1,'/');
		current_path.append(path_tree[i]);
	}
	
	return 0;
}


void init_file_access_control_API(size_t num_workers)
{
	disable_access_control_API = false;
	if(is_server_config_variable_true("disable_file_access_API"))
		disable_access_control_API = true;
		
	max_AD_cache_size = str2uint(&SERVER_CONFIGURATION["max_file_access_cache_size"]) * 1000;
	
	access_descriptor_cache = std::vector<std::unordered_map<std::string, struct access_descriptor_cache_entry>>(num_workers);
	access_descriptor_list = std::vector<std::list<std::string>>(num_workers);
}

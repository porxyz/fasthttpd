#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
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

std::unordered_map <std::string,struct access_descriptor> access_descriptor_list;
std::mutex access_descriptors_mutex;

inline bool access_descriptor_exists(const std::string* path)
{
	bool found = false;

	access_descriptors_mutex.lock();
	
	if(access_descriptor_list.find(path[0]) != access_descriptor_list.end())
		found = true;
	
	access_descriptors_mutex.unlock();


	return found;
}


inline bool pattern_compare(const std::string& needle,const std::regex& haystack)
{
	return std::regex_match(needle,haystack);
}

void read_access_descriptor_file(const std::string* path)
{
	struct access_descriptor result;
	std::string access_configuration_file_path = path[0];

	access_configuration_file_path.append("/.access_config");

	std::ifstream access_configuration_file;
	access_configuration_file.open(access_configuration_file_path.c_str());

	if(!access_configuration_file.is_open())
	{
		result.defined = false;
		access_configuration_file.close();
		access_descriptors_mutex.lock();
		access_descriptor_list[path[0]] = result;
		access_descriptors_mutex.unlock();
		
		return;
	}

	result.defined = true;

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

		result.entry_list.push_back(std::regex(escaped_haystack));
	}

	access_configuration_file.close();


	if(config_params_map.find(std::string("order")) != config_params_map.end())
	{
		if(config_params_map["order"] == std::string("allow"))
			result.order_deny = false;
			
			
		else
			result.order_deny = true;
			
	}
	else
		result.order_deny = true;


	if(config_params_map.find(std::string("fake_404")) != config_params_map.end())
	{
		if(config_params_map["fake_404"] == std::string("true") or config_params_map["fake_404"] == std::string("1"))
			result.fake_404 = true;
			
			
		else
			result.fake_404 = false;
	}
	else
		result.fake_404 = false;


	access_descriptors_mutex.lock();
	access_descriptor_list[path[0]] = result;
	access_descriptors_mutex.unlock();
}


int measure_host_path_depth(const std::string* filename)
{
	int path_depth = 0;
	std::vector<std::string> path_tree;
	explode(filename,"/",&path_tree);

	for(size_t i=0; i < path_tree.size(); i++)
	{

		if(path_tree[i] == "." or path_tree[i].size() == 0)
			continue;

		path_depth+=1;
	}

	return path_depth;
}

int check_file_access(const std::string* filename,const std::string* host_path)
{
	std::vector<std::string> path_tree;
	explode(filename,"/",&path_tree);
	
	
	// max path depth server config
	if(path_tree.size() > 64)
		return 400;


	std::string current_path;
	int path_depth = 0;
	struct access_descriptor* desc;

	int host_path_depth = measure_host_path_depth(host_path);

	if(filename[0][0] == '/')
		path_tree[1].insert(0,1,'/');


	for(size_t i=0; i < path_tree.size(); i++)
	{

		if(path_tree[i] == "." or path_tree[i].size() == 0)
			continue;

		if(path_tree[i] == ".." and path_depth < host_path_depth)
		{
			current_path.append("/..");
			path_depth+=1;
			continue;
		}

		if(path_tree[i] == ".." and path_depth > host_path_depth)
		{
			current_path = current_path.substr(0,current_path.find_last_of('/'));
			path_depth-=1;
			continue;
		}


		if(path_depth >= host_path_depth and path_tree[i] != "..")
		{

			if(!access_descriptor_exists(&current_path))
				read_access_descriptor_file(&current_path);


			desc = &access_descriptor_list[current_path];
			if(desc->defined)
			{
				bool match = false;
				for(size_t j = 0; j<desc->entry_list.size(); j++)
				{
					if(pattern_compare(path_tree[i],desc->entry_list[j]))
					{
						match = true;
						break;
					}
				}

				if(match and desc->order_deny)
					return (desc->fake_404) ? 404 : 403;
					
				if(!match and !desc->order_deny)
					return (desc->fake_404) ? 404 : 403;

			}


		}

		if(path_tree[i] != "..")
		{
			if(path_depth != 0)
				current_path.append("/");
				
			current_path.append(path_tree[i]);
			path_depth+=1;
		}

	}

	return 0;
}



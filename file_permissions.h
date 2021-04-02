#ifndef __file_permissions_incl__
#define __file_permissions_incl__

#include <string>

int check_file_access(const std::string* filename,const std::string* host_path);
int measure_host_path_depth(const std::string* filename);

#endif

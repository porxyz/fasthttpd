#ifndef __file_permissions_incl__
#define __file_permissions_incl__

#include <string>
#include <cstdint>

void init_file_access_control_API(size_t num_workers);
int check_file_access(size_t worker_id,const std::string& filename,const std::string& host_path);

#endif

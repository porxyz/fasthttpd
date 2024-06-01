#!/usr/bin/env python3

import os
import sys

enable_MOD_MYSQL = True
enable_https = True
debug = False

COMPILER = "clang++"
COMPILER_FLAGS = "-std=c++11 -Wall -Wfatal-errors -pthread"
LLIBS = "-lnghttp2"

if debug:
	COMPILER_FLAGS += " -O0 -ggdb -rdynamic"
else:
	COMPILER_FLAGS += " -O3"


if enable_https:
	LLIBS += " -lssl -lcrypto"
else:
	COMPILER_FLAGS += " -DDISABLE_HTTPS"
	
	
if enable_MOD_MYSQL:
	LLIBS += " -lmysqlclient"	
else:
	COMPILER_FLAGS += " -DNO_MOD_MYSQL"
	

def get_source_dependencies(source_file):
	compiler_output = os.popen(COMPILER + " " + source_file + " -MM").read()
	compiler_output = compiler_output.replace("\\"," ").replace("\n", " ").replace("\r","")
	compiler_output = compiler_output[compiler_output.find(": ") + 2 : ]
	dependencies = compiler_output.split(" ")
	return [header for header in dependencies if len(header) > 0]


def source_code_modified(source_file,object_file):

	# object does not exist and should be compiled
	if not os.path.isfile(object_file):
		return True
		
	object_last_mod_time = os.path.getmtime(object_file)

	source_files = get_source_dependencies(source_file)
		 
	for i in range(0,len(source_files)): #iterate timestamps
		#A source code file was modified since last compilation
		if os.path.getmtime(source_files[i]) > object_last_mod_time:
			return True
	
	return False #The object is up to date




def compile_helper_functions():
	need_to_build = False
	
	if source_code_modified("../helper_functions.cpp","helper_functions.o") and len(sys.argv) < 3:
		need_to_build = True	

	if len(sys.argv) >= 3 and sys.argv[1].lower() == "compile" and sys.argv[2].lower() == "helper_functions":
		 need_to_build = True
		 
	if need_to_build:
		print("Building the helper functions")
		compiler_return_value = os.system(COMPILER + " -c ../helper_functions.cpp " + COMPILER_FLAGS)
		if compiler_return_value != 0:
			print("Can not compile the helper functions")
			exit()


def compile_server_config():
	need_to_build = False
	
	if source_code_modified("../server_config.cpp","server_config.o") and len(sys.argv) < 3:
		need_to_build = True
		
	if len(sys.argv) >= 3 and sys.argv[1].lower() == "compile" and sys.argv[2].lower() == "server_config":
		need_to_build = True
		
	if need_to_build:
		print("Building the server configuration API")
		compiler_return_value = os.system(COMPILER + " -c ../server_config.cpp " + COMPILER_FLAGS)
		if compiler_return_value != 0:
			print("Can not compile the server configuration API");
			exit()


def compile_server_log():
	need_to_build = False
	
	if source_code_modified("../server_log.cpp","server_log.o") and len(sys.argv) < 3:
		need_to_build = True
		
	if len(sys.argv) >= 3 and sys.argv[1].lower() == "compile" and sys.argv[2].lower() == "server_log":
		need_to_build = True
		
	if need_to_build:
		print("Building the server log API")
		compiler_return_value = os.system(COMPILER + " -c ../server_log.cpp " + COMPILER_FLAGS)
		if compiler_return_value != 0:
			print("Can not compile the server log API");
			exit()
			
			
def compile_file_permissions():
	need_to_build = False
	
	if source_code_modified("../file_permissions.cpp","file_permissions.o") and len(sys.argv) < 3:
		need_to_build = True
		
	if len(sys.argv) >= 3 and sys.argv[1].lower() == "compile" and sys.argv[2].lower() == "file_permissions":
		need_to_build = True
		
	if need_to_build:
		print("Building the file permissions API")
		compiler_return_value = os.system(COMPILER + " -c ../file_permissions.cpp " + COMPILER_FLAGS)
		if compiler_return_value != 0:
			print("Can not compile the file permissions API");
			exit()
			

def compile_http_parser():
	need_to_build = False
	
	if source_code_modified("../http_worker/http_parser.cpp","http_parser.o") and len(sys.argv) < 3:
		need_to_build = True
		
	if len(sys.argv) >= 3 and sys.argv[1].lower() == "compile" and sys.argv[2].lower() == "http_parser":
		need_to_build = True
		
	if need_to_build:
		print("Building the http parser API")
		compiler_return_value = os.system(COMPILER + " -c ../http_worker/http_parser.cpp " + COMPILER_FLAGS)
		if compiler_return_value != 0:
			print("Can not compile the http parser API");
			exit()
			

def compile_http1_conn_processor():
	need_to_build = False
	
	if source_code_modified("../http_worker/http1_connection_processor.cpp","http1_connection_processor.o") and len(sys.argv) < 3:
		need_to_build = True
		
	if len(sys.argv) >= 3 and sys.argv[1].lower() == "compile" and sys.argv[2].lower() == "http1_connection_processor":
		need_to_build = True
		
	if need_to_build:
		print("Building the http/1.1 connection processor")
		compiler_return_value = os.system(COMPILER + " -c ../http_worker/http1_connection_processor.cpp " + COMPILER_FLAGS)
		if compiler_return_value != 0:
			print("Can not compile the http/1.1 connection processor");
			exit()


def compile_http2_conn_processor():
	need_to_build = False
	
	if source_code_modified("../http_worker/http2_connection_processor.cpp","http2_connection_processor.o") and len(sys.argv) < 3:
		need_to_build = True
		
	if len(sys.argv) >= 3 and sys.argv[1].lower() == "compile" and sys.argv[2].lower() == "http2_connection_processor":
		need_to_build = True
		
	if need_to_build:
		print("Building the http/2 connection processor")
		compiler_return_value = os.system(COMPILER + " -c ../http_worker/http2_connection_processor.cpp " + COMPILER_FLAGS)
		if compiler_return_value != 0:
			print("Can not compile the http/2 connection processor");
			exit()

def compile_http2_stream_processor():
	need_to_build = False
	
	if source_code_modified("../http_worker/http2_stream_processor.cpp","http2_stream_processor.o") and len(sys.argv) < 3:
		need_to_build = True
		
	if len(sys.argv) >= 3 and sys.argv[1].lower() == "compile" and sys.argv[2].lower() == "http2_stream_processor":
		need_to_build = True
		
	if need_to_build:
		print("Building the http/2 stream processor")
		compiler_return_value = os.system(COMPILER + " -c ../http_worker/http2_stream_processor.cpp " + COMPILER_FLAGS)
		if compiler_return_value != 0:
			print("Can not compile the http/2 stream processor");
			exit()
			

def compile_http2_frame_processor():
	need_to_build = False
	
	if source_code_modified("../http_worker/http2_frame_processor.cpp","http2_frame_processor.o") and len(sys.argv) < 3:
		need_to_build = True
		
	if len(sys.argv) >= 3 and sys.argv[1].lower() == "compile" and sys.argv[2].lower() == "http2_frame_processor":
		need_to_build = True
		
	if need_to_build:
		print("Building the http/2 frame processor")
		compiler_return_value = os.system(COMPILER + " -c ../http_worker/http2_frame_processor.cpp " + COMPILER_FLAGS)
		if compiler_return_value != 0:
			print("Can not compile the http/2 frame processor");
			exit()


def compile_http_request_processor():
	need_to_build = False
	
	if source_code_modified("../http_worker/request_processor.cpp","request_processor.o") and len(sys.argv) < 3:
		need_to_build = True
		
	if len(sys.argv) >= 3 and sys.argv[1].lower() == "compile" and sys.argv[2].lower() == "http_request_processor":
		need_to_build = True
		
	if need_to_build:
		print("Building the http request processor")
		compiler_return_value = os.system(COMPILER + " -c ../http_worker/request_processor.cpp " + COMPILER_FLAGS)
		if compiler_return_value != 0:
			print("Can not compile the http request processor");
			exit()


def compile_hpack_api():
	need_to_build = False
	
	if source_code_modified("../http_worker/hpack_api.cpp","hpack_api.o") and len(sys.argv) < 3:
		need_to_build = True
		
	if len(sys.argv) >= 3 and sys.argv[1].lower() == "compile" and sys.argv[2].lower() == "hpack_api":
		need_to_build = True
		
	if need_to_build:
		print("Building the http/2 header compression API")
		compiler_return_value = os.system(COMPILER + " -c ../http_worker/hpack_api.cpp " + COMPILER_FLAGS)
		if compiler_return_value != 0:
			print("Can not compile the http/2 header compression API");
			exit()


def compile_http_worker():
	
	#compile http worker submodules
	compile_http_parser()
	compile_http1_conn_processor()
	compile_http2_conn_processor()
	compile_http2_stream_processor()
	compile_http2_frame_processor()
	compile_http_request_processor()
	compile_hpack_api()

	need_to_build = False
	
	if source_code_modified("../http_worker/http_worker.cpp","http_worker.o") and len(sys.argv) < 3:
		need_to_build = True
		
	if len(sys.argv) >= 3 and sys.argv[1].lower() == "compile" and sys.argv[2].lower() == "http_worker":
		need_to_build = True
		
	if need_to_build:
		print("Building the http worker")
		compiler_return_value = os.system(COMPILER + " -c ../http_worker/http_worker.cpp " + COMPILER_FLAGS)
		if compiler_return_value != 0:
			print("Can not compile the http worker API");
			exit()
	
			
def compile_server_listener():
	need_to_build = False
	
	if source_code_modified("../server_listener.cpp","server_listener.o") and len(sys.argv) < 3:
		need_to_build = True
		
	if len(sys.argv) >= 3 and sys.argv[1].lower() == "compile" and sys.argv[2].lower() == "server_listener":
		need_to_build = True
		
	if need_to_build:
		print("Building the server listener API")
		compiler_return_value = os.system(COMPILER + " -c ../server_listener.cpp " + COMPILER_FLAGS)
		if compiler_return_value != 0:
			print("Can not compile the server listener API");
			exit()
			
			

def compile_https_listener():
	need_to_build = False
	
	if source_code_modified("../https_listener.cpp","https_listener.o") and len(sys.argv) < 3:
		need_to_build = True
		
	if len(sys.argv) >= 3 and sys.argv[1].lower() == "compile" and sys.argv[2].lower() == "https_listener":
		need_to_build = True
		
	if need_to_build:
		print("Building the https listener API")
		compiler_return_value = os.system(COMPILER + " -c ../https_listener.cpp " + COMPILER_FLAGS)
		if compiler_return_value != 0:
			print("Can not compile the https listener API");
			exit()
	
			
def compile_MOD_MYSQL():
	need_to_build = False
	
	if source_code_modified("../mod_mysql.cpp","mod_mysql.o") and len(sys.argv) < 3:
		need_to_build = True
		
	if len(sys.argv) >= 3 and sys.argv[1].lower() == "compile" and sys.argv[2].lower() == "mod_mysql":
		need_to_build = True
		
	if need_to_build:
		print("Building the MYSQL support module")
		compiler_return_value = os.system(COMPILER + " -c ../mod_mysql.cpp " + COMPILER_FLAGS)
		if compiler_return_value != 0:
			print("Can not compile the MYSQL support module");
			exit()
			

def compile_custom_bound():
	need_to_build = False
	
	if source_code_modified("../custom_bound.cpp","custom_bound.o") and len(sys.argv) < 3:
		need_to_build = True
		
	if len(sys.argv) >= 3 and sys.argv[1].lower() == "compile" and sys.argv[2].lower() == "custom_bound":
		need_to_build = True
		
	if need_to_build:
		print("Building the custom code framework")
		compiler_return_value = os.system(COMPILER + " -c ../custom_bound.cpp " + COMPILER_FLAGS)
		if compiler_return_value != 0:
			print("Can not compile the custom code framework");
			exit()

def compile_main():
	need_to_build = False
	
	if source_code_modified("../main.cpp","main.o") and len(sys.argv) < 3:
		need_to_build = True
		
	if len(sys.argv) >= 3 and sys.argv[1].lower() == "compile" and sys.argv[2].lower() == "main":
		need_to_build = True
		
	if need_to_build:
		print("Building the server")
		compiler_return_value = os.system(COMPILER + " -c ../main.cpp " + COMPILER_FLAGS)
		if compiler_return_value != 0:
			print("Can not build the server");
			exit()


if len(sys.argv) >= 2 and sys.argv[1].lower() == "clean":
	rm_return_value = os.system("rm -Rf build")
	if rm_return_value != 0:
		print("Unable to delete the build directory")
		
	exit()



if not os.path.isdir("build"):
		try: 
    			os.mkdir("build",0o755) 
		except OSError: 
    			print("Can not create the build directory")
    			exit()
    			
if len(sys.argv) == 1 or (len(sys.argv) >= 2 and sys.argv[1].lower() == "compile"):    		  
	os.chdir("build")

	compile_helper_functions()
	compile_server_config()
	compile_server_log()
	compile_file_permissions()
	compile_http_worker()
	compile_server_listener()

	if enable_https:
		compile_https_listener()
	
	if enable_MOD_MYSQL:
		compile_MOD_MYSQL()
		
	
	compile_main()
	
	#code for compiling custom modules start here
	#
	#code for compiling custom modules end here

	compile_custom_bound()

	os.chdir("..")


os.chdir("build")
all_objects = os.listdir()

should_link = False
if not os.path.isfile("fasthttpd") or (len(sys.argv) >= 2 and sys.argv[1].lower() == "link"):
	should_link = True
else:
	object_last_mod_time = os.path.getmtime("fasthttpd")
	for i in range(0,len(all_objects)): 
		if os.path.getmtime(all_objects[i]) > object_last_mod_time:
			should_link = True
			break
			

if should_link:
	print("Linking all objects into fasthttpd")
	compiler_return_value = os.system(COMPILER + " -o fasthttpd *.o " + LLIBS + " " + COMPILER_FLAGS)
	if compiler_return_value != 0:
		print("Can not link fasthttpd")

should_clean = False
if len(sys.argv) >= 2 and sys.argv[1].lower() == "clean":
	should_clean = True

if should_clean:
	os.system("rm -R -f build")
	






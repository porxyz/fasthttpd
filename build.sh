#!/usr/bin/php
<?php
error_reporting(E_ERROR | E_WARNING);

$enable_MOD_MYSQL = true;

$COMPILER = "g++";
$COMPILER_FLAGS = "-rdynamic -ggdb -Wall -Wfatal-errors -O3";
$LLIBS = "-lpthread -lssl -lcrypto";


if($enable_MOD_MYSQL)
	$LLIBS.=" -lmysqlclient";
	
else
	$COMPILER_FLAGS.=" -DNO_MOD_MYSQL";

function source_code_modified($source_files,$object_file)
{
	$object_last_mod_time = @filemtime($object_file);
	
	// object does not exist and should be compiled
	if($object_last_mod_time === FALSE)
		return TRUE;

	//if a single filename string is given convert it to array; ex: 0 => filename
	if(!is_array($source_files))
		$source_files = array($source_files);
		 

	for($i=0; $i<count($source_files); $i++) // iterate timestamps
	{
		// A source code file was modified since last compilation
		if(filemtime($source_files[$i]) > $object_last_mod_time)
			return TRUE;
	}

	return FALSE; // The object is up to date
}



if(strcasecmp($argv[1],"clean") === 0)
{
	$rm_return_value = 0;
	system("rm -Rf build",$rm_return_value);
	if($rm_return_value != 0)
		die("Unable to delete build directory\n");
		
	exit();
}



$compiler_return_value = 0;

if(count($argv) === 1 or strcasecmp($argv[1],"build") === 0)
{

	if(!file_exists("build"))
	{
		if(mkdir("build") === FALSE)
			die("Can not create build directory\n");
	}

	chdir("build");


	//compile helper functions
	if(source_code_modified(array("../helper_functions.h","../helper_functions.cpp"),"helper_functions.o") or strcasecmp($argv[2],"helper_functions") === 0)
	{
		echo("Building helper functions\n");
		system(($COMPILER . " -c ../helper_functions.cpp " . $COMPILER_FLAGS),$compiler_return_value);
		if($compiler_return_value != 0)
			die("Can not compile helper functions\n");
			
		if(strcasecmp($argv[2],"helper_functions") === 0)
			exit();
	}


	//compile server config API
	if(source_code_modified(array("../server_config.h","../server_config.cpp"),"server_config.o") or strcasecmp($argv[2],"server_config") === 0)
	{
		echo("Building server configuration API\n");
		system(($COMPILER . " -c ../server_config.cpp " . $COMPILER_FLAGS),$compiler_return_value);
		if($compiler_return_value != 0)
			die("Can not compile server configuration API\n");
			
		if(strcasecmp($argv[2],"server_config") === 0)
			exit();
	}

	//compile server journal API
	if(source_code_modified(array("../server_journal.h","../server_journal.cpp"),"server_journal.o") or strcasecmp($argv[2],"server_journal") === 0)
	{
		echo("Building server journal API\n");
		system(($COMPILER . " -c ../server_journal.cpp " . $COMPILER_FLAGS),$compiler_return_value);
		if($compiler_return_value != 0)
			die("Can not compile server journal API\n");
			
		if(strcasecmp($argv[2],"server_journal") === 0)
			exit();
	}

	//compile file permissions API
	if(source_code_modified(array("../file_permissions.h","../file_permissions.cpp"),"file_permissions.o") or strcasecmp($argv[2],"file_permissions") === 0)
	{
		echo("Building file permissions API\n");
		system(($COMPILER . " -c ../file_permissions.cpp " . $COMPILER_FLAGS),$compiler_return_value);
		if($compiler_return_value != 0)
			die("Can not compile file permissions API\n");
			
		if(strcasecmp($argv[2],"file_permissions") === 0)
			exit();
	}

	//compile http worker
	if(source_code_modified(array("../http_worker.h","../http_worker.cpp"),"http_worker.o") or strcasecmp($argv[2],"http_worker") === 0)
	{
		echo("Building http worker\n");
		system(($COMPILER . " -c ../http_worker.cpp " . $COMPILER_FLAGS),$compiler_return_value);
		if($compiler_return_value != 0)
			die("Can not compile http worker API\n");
			
		if(strcasecmp($argv[2],"http_worker") === 0)
			exit();
	}

	//compile https listener thread
	if(source_code_modified(array("../https_listener.h","../https_listener.cpp"),"https_listener.o") or strcasecmp($argv[2],"https_listener") === 0)
	{
		echo("Building https listener thread\n");
		system(($COMPILER . " -c ../https_listener.cpp " . $COMPILER_FLAGS),$compiler_return_value);
		if($compiler_return_value != 0)
			die("Can not compile https listener thread\n");
			
		if(strcasecmp($argv[2],"https_listener") === 0)
			exit();
	}

	//compile MYSQL support module
	if((source_code_modified(array("../mod_mysql.h","../mod_mysql.cpp"),"mod_mysql.o") or strcasecmp($argv[2],"mod_mysql") === 0) and $enable_MOD_MYSQL)
	{
		echo("Building MYSQL support module\n");
		system(($COMPILER . " -c ../mod_mysql.cpp " . $COMPILER_FLAGS),$compiler_return_value);
		if($compiler_return_value != 0)
			die("Can not compile MYSQL support module\n");
			
		if(strcasecmp($argv[2],"mod_mysql") === 0)
			exit();
	}


	//compile server
	if(source_code_modified(array("../main.cpp"),"main.o") or strcasecmp($argv[2],"main") === 0)
	{
		echo("Building server\n");
		system(($COMPILER . " -c ../main.cpp " . $COMPILER_FLAGS),$compiler_return_value);
		if($compiler_return_value != 0){die("Can not compile server\n");}
		if(strcasecmp($argv[2],"main") === 0){exit();}
	}



	//code for compiling custom modules start here




	//code for compiling custom modules end here



	//compile custom code framework
	if(source_code_modified(array("../custom_bound.h","../custom_bound.cpp"),"custom_bound.o") or strcasecmp($argv[2],"custom_bound") === 0)
	{
		echo("Building custom code framework\n");
		system(($COMPILER . " -c ../custom_bound.cpp " . $COMPILER_FLAGS),$compiler_return_value);
		if($compiler_return_value != 0)
			die("Can not compile custom code framework\n");
			
		if(strcasecmp($argv[2],"custom_bound") === 0)
			exit();
	}

}





$all_objects = array();

$h_folder = opendir(".");
$filename = "";
while (false !== ($filename = readdir($h_folder))) 
{
	if($filename[strlen($filename) - 1] === 'o' and $filename[strlen($filename) - 2] === '.')
		$all_objects[] = $filename;
}
closedir($h_folder);


if(source_code_modified($all_objects,"server.bin") or strcasecmp($argv[1],"link") === 0)
{
	echo("Linking all objects into server.bin\n");
	system(($COMPILER . " -o server.bin *.o " . $LLIBS . " " . $COMPILER_FLAGS),$compiler_return_value);
	if($compiler_return_value != 0)
		die("Can not link server.bin\n");
}



?>


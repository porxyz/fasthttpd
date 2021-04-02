#!/usr/bin/php
<?php

$netstat_command = "netstat -ltp";

$terminal_output = array();
$netstat_retval = 0;

if(exec($netstat_command,$terminal_output,$netstat_retval) === FALSE or $netstat_retval != 0)
	die("netstat did not completed successfully\n\n");
	


$table_head_row=-1;
for($i=0; $i<count($terminal_output); $i++)
{
	if(strpos($terminal_output[$i],"Local Address") and strpos($terminal_output[$i],"State"))
	{
		$table_head_row=$i;
		break;
	}
}

if($table_head_row == -1)
	die("Unable to parse netstat output\n\n");
	

$table_head_s = str_replace("Local Address","Local-Address",$terminal_output[$table_head_row]);
$table_head_s = str_replace("Foreign Address","Foreign-Address",$table_head_s);
$table_head_s = str_replace("PID/Program name","PID/Program-name",$table_head_s);



$netstat_table_head = array_values(array_filter(explode(" ",$table_head_s),"strlen"));

$local_addr_column = -1;
$pid_column = -1;
	
for($i=0; $i<count($netstat_table_head); $i++)
{
	if(strpos($netstat_table_head[$i],"Local-Address") !== FALSE)
		$local_addr_column=$i;
	
		
	if(strpos($netstat_table_head[$i],"PID/Program-name") !== FALSE)
		$pid_column=$i;
	
}

if($local_addr_column == -1 or $pid_column==-1)
	die("Unable to parse netstat output\n\n");


$target_row=-1;
for($i=$table_head_row+1; $i<count($terminal_output); $i++)
{
	$table_row = array_values(array_filter(explode(" ",$terminal_output[$i]),"strlen"));
	
	$listen_addr = explode(":",$table_row[$local_addr_column]);
	if($listen_addr[count($listen_addr)-1] == 80 or $listen_addr[count($listen_addr)-1] == "http")
	{
		$target_row=$i;
		break;
	}
}

if($target_row == -1)
	die("The server is not running\n\n");
	
	
$table_row = array_values(array_filter(explode(" ",$terminal_output[$target_row]),"strlen"));	

if($table_row[$pid_column] == "-")
	die("Unable to read the server PID!\nAre you root?\n\n");



$pid = (explode("/",$table_row[$pid_column]))[0];
	

exec(("kill ".$pid));

?>





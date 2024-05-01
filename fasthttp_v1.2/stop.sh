#!/usr/bin/env bash

#server stop script
#assuming that the server is running on port 80

PORT=80
PID_LIST=$(lsof -t -i:$PORT -sTCP:LISTEN)
SERVER_NAME="fasthttpd"

echo $PID_LIST | while read line; do
	PROCESS_PATH=$(readlink -f /proc/$line/exe)
	
	if [[ "$PROCESS_PATH" =~ "$SERVER_NAME" ]]; 
	then
		kill $line > /dev/null 2>&1
	fi

done


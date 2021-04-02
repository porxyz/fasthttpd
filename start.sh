#!/bin/bash
clear

#start server
#valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./build/server.bin main.conf
./build/server.bin main.conf

if [ $? -ne 0 ] ; then
  echo "Server exited with error"
  exit -1
fi





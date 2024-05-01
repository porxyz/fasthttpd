#!/usr/bin/env bash
clear

#start server
#valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --track-fds=yes --leak-resolution=high --trace-children=yes -s --log-file="valgrind_dump.txt" ./build/fasthttpd main.conf
./build/fasthttpd main.conf

if [ $? -ne 0 ] ; then
  echo "fasthttpd exited with error"
  exit -1
fi





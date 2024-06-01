#!/usr/bin/env bash

if [ `id -u` -ne 0 ] 
then
  echo "You need to run this script with admin-level privileges!"
  read -p "Do you want to proceed anyway? y/n" choice

  case $choice in
    [yY]* ) ;;
    *) exit ;;
  esac

fi

#copy the compiled binary
mkdir -p /opt/fasthttpd
cp -f build/fasthttpd /opt/fasthttpd/fasthttpd

#copy the configuration
mkdir -p /etc/fasthttpd
cp -f main.conf /etc/fasthttpd/main.conf
cp -rf config /etc/fasthttpd/config

#create the log directory
mkdir -p /var/log/fasthttpd

#create the log directory
mkdir -p /var/fasthttpd
cp -rf www /var/fasthttpd/www

#copy the systemd unit
cp -f fasthttpd.service /etc/systemd/system/fasthttpd.service

#install the systemd service
systemctl daemon-reload
systemctl start fasthttpd.service
systemctl enable fasthttpd.service



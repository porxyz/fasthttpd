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

#remove the files
rm -rf /opt/fasthttpd
rm -rf /etc/fasthttpd
rm -rf /var/log/fasthttpd


#remove the systemd unit
systemctl stop fasthttpd.service
systemctl disable fasthttpd.service
rm -f /etc/systemd/system/fasthttpd.service
systemctl daemon-reload



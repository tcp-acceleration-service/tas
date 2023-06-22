#!/usr/bin/env bash

if [ "$#" -ne 3 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[br_name, tun_name, remote_ip]"
    exit
fi

br_name=$1
tun_name=$2
remote_ip=$3

ovs-vsctl add-port $br_name $tun_name \
    -- set interface $tun_name type=gre options:remote_ip=$remote_ip
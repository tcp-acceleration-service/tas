#!/usr/bin/env bash

if [ "$#" -gt 4 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[br_name, tun_name, remote_ip, tun_id]"
    exit
fi

br_name=$1
tun_name=$2
remote_ip=$3
tun_id=$4

ovs-vsctl add-port $br_name $tun_name -- \
    set interface $tun_name type=gre options:remote_ip=$remote_ip options:key=$tun_id 
# ovs-vsctl add-port $br_name $tun_name -- \
#     set interface $tun_name type=gre options:remote_ip=$remote_ip options:key=$tun_id 
# ovs-vsctl add-port $br_name $tun_name -- \
#     set interface $tun_name type=gre \
#     options:remote_ip=flow options:local_ip=flow options:key=flow
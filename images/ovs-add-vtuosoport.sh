#!/usr/bin/env bash

if [ "$#" -lt 3 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[br_name, port_name, type, vmid, out_remote_ip, out_local_ip, in_remote_ip, in_local_ip, key]"
    exit
fi

br_name=$1
port_name=$2
type=$3
vmid=$4
out_remote_ip=$5
out_local_ip=$6
in_remote_ip=$7
in_local_ip=$8
key=$9

if [[ "$type" == "virtuosotx" ]]
then
    echo "Adding virtuosotx port"
    ovs-vsctl add-port $br_name $port_name -- set Interface $port_name type=$type \
        options:out_remote_ip=$out_remote_ip options:out_local_ip=$out_local_ip \
        options:in_remote_ip=$in_remote_ip options:in_local_ip=$in_local_ip \
        options:key=$key 
else
    echo "Adding regular Virtuoso port"
    ovs-vsctl add-port $br_name $port_name -- set Interface $port_name type=$type vmid=$vmid
fi
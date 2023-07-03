#!/usr/bin/env bash

if [ "$#" -ne 5 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[br_name vhost_name gre_name remote_ip gre_key]"
    exit
fi

br_name=$1
vhost_name=$2
gre_name=$3
remote_ip=$4
gre_key=$5

echo "Set bridge datapath type to netdev"
ovs-vsctl set bridge ${br_name} datapath_type=netdev

echo "Adding interface ${vhost_name} to port"
ovs-vsctl add-port ${br_name} ${vhost_name} -- set Interface ${vhost_name} type=dpdkvhostuser

echo "Adding GRE port"
ovs-vsctl add-port br0 $gre_name -- set interface $gre_name type=gre options:remote_ip=$remote_ip options:tunnel_id=$gre_key
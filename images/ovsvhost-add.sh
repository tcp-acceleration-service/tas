#!/usr/bin/env bash

if [ "$#" -ne 2 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[br_name vhost_name]"
    exit
fi

br_name=$1
vhost_name=$2

echo "Set bridge datapath type to netdev"
ovs-vsctl set bridge ${br_name} datapath_type=netdev

echo "Adding interface ${vhost_name} to port"
ovs-vsctl add-port ${br_name} ${vhost_name} -- set Interface ${vhost_name} type=dpdkvhostuser


#!/usr/bin/env bash

if [ "$#" -ne 3 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[br_name ip interface]"
    exit
fi

br_name=$1
ip=$2
interface=$3

# Create bridge using OvS
ovs-vsctl add-br $br_name
ovs-vsctl add-port $br_name $interface

# Delete ip config from interface so that it can be added to bridge
sudo ip addr del $ip dev $interface

# Add ip address to bridge
sudo ip addr add $ip dev $br_name
ifconfig $br_name up
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
echo "Creating bridge ${br_name} using OvS"
ovs-vsctl add-br $br_name
echo "Set bridge ${br_name} datapath type to netdev"
ovs-vsctl set bridge $br_name datapath_type=netdev
echo "Adding interface ${interface} to port"
ovs-vsctl add-port $br_name $interface 
echo "Set dpdkvhost interface ${interface}"
ovs-vsctl set Interface $interface type=dpdkvhostuser options:n_rxq=12

# Delete ip config from interface so that it can be added to bridge
echo "Delete ip config from interface ${interface}"
sudo ip addr del $ip dev $interface

# Add ip address to bridge
echo "Add ip ${ip} to bridge ${br_name}"
sudo ip addr add $ip dev $br_name
ifconfig $br_name up
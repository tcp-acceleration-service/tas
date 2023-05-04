#!/usr/bin/env bash

# Creates a dummy networking interface with an ip and mac address

set -o errexit

if [ "$#" -ne 3 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[interface, ip, mac]"
    exit
fi

# The name of the networking interface
interface=$1
# Ip of the interface
ip=$2
# The MAC address of the interface
mac=$3


sudo modprobe dummy
sudo ip link add $interface type dummy
echo "Added interface $interface"

sudo ifconfig eth0 hw ether $mac
echo "Added MAC address $mac to $interface"

sudo ip addr add $ip brd + dev $interface label $interface:0
echo "Added ip address $ip to interface"

sudo ip link set dev eth0 up
echo "$interface is up"
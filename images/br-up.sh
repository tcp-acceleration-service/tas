#!/usr/bin/env bash

# Creates a bridge and adds the current networking interface
# to the bridge

set -o errexit

if [ "$#" -ne 2 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[interface, ip]"
    exit
fi

# The normal networking interface (in our case the Mellanox)
interface=$1
# Ip of the interface
ip=$2

sudo brctl addbr br0
echo "created br0"

sudo ip addr flush dev $interface
sudo brctl addif br0 $interface
echo "added ${interface} to br0"

sudo ifconfig $interface up
echo "interface ${interface} is up"

sudo ifconfig br0 up
echo "br0 is up"

sudo ip addr add $ip dev br0
echo "added ip=${ip} to br0"

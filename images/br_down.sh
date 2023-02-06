#!/usr/bin/env bash

# Brings down the bridge and restores the regular
# networking interface

if [ "$#" -ne 2 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[interface, ip]"
    exit
fi

# The normal networking interface (in our case the Mellanox)
interface=$1
# Ip of the bridge that will be added to the interface
ip=$2

sudo brctl delif br0 $interface
echo "deleted ${interface} from bridge"

sudo ifconfig br0 down
sudo brctl delbr br0
echo "deleted br0"

sudo ifconfig $interface up
sudo ip addr add $ip dev $interface
echo "added ip=${ip} back to ${interface}"

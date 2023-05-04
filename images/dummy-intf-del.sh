#!/usr/bin/env bash

# Removes dummy interface

set -o errexit

if [ "$#" -ne 2 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[interface, ip]"
    exit
fi

# The name of the networking interface
interface=$1
# Ip of the interface
ip=$2

sudo ip addr del $ip brd + dev $interface label $interface:0
echo "Deleted $ip from $interface"

sudo ip link delete $interface type dummy
echo "Deleted $interface"

sudo rmmod dummy
#!/usr/bin/env bash

if [ "$#" -ne 2 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[br_name tap_name]"
    exit
fi

br_name=$1
tap_name=$2

ip tuntap add mode tap $tap_name
ifconfig $tap_name up

ovs-vsctl add-port $br_name $tap_name
#!/usr/bin/env bash

if [ "$#" -ne 3 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[br_name tap_name multi_queue]"
    exit
fi

br_name=$1
tap_name=$2
multi_queue=$3

if [ $multi_queue -eq 1 ]; then
    sudo ip tuntap add mode tap multi_queue name $tap_name
    echo "Set ${tap_name} for multi queue"
else
    ip tuntap add mode tap $tap_name
fi

ifconfig $tap_name up

ovs-vsctl add-port $br_name $tap_name
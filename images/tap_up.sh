#!/usr/bin/env bash

set -o errexit

if [ "$#" -ne 2 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[tap_name multi_queue]"
    exit
fi

# Name of tap device
name=$1
multi_queue=$2

if [ $multi_queue -eq 1 ]; then
    sudo ip tuntap add mode tap multi_queue name $name
    echo "Set ${name} for multi queue"
else
    sudo tunctl -t $name -u `whoami`
fi
echo "Created tap device ${name}"

sudo brctl addif br0 $name
echo "Added tap device to br0"
sudo ifconfig $name up
echo "${name} is up"

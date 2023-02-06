#!/usr/bin/env bash


if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[tap_name]"
    exit
fi

# Name of tap device
name=$1

sudo brctl delif br0 $name
echo "Deleted ${name} from br0"

sudo ip link delete dev $name
echo "Deleted ${name}"

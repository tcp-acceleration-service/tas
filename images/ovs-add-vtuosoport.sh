#!/usr/bin/env bash

if [ "$#" -ne 2 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[br_name, port_name]"
    exit
fi

br_name=$1
port_name=$2

ovs-vsctl add-port $br_name $port_name -- set Interface $port_name type=virtuoso
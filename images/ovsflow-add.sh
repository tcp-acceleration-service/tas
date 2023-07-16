#!/usr/bin/env bash

if [ "$#" -ne 3 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[br_name in_port out_port]"
    exit
fi

br_name=$1
in_port=$2
out_port=$3

sudo ovs-ofctl add-flow br0 "table=0, priority=100, in_port=$in_port, actions=output:$out_port"
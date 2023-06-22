#!/usr/bin/env bash

if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[br_name]"
    exit
fi

br_name=$1

ovs-vsctl add-br $br_name -- set bridge $br_name datapath_type=netdev
#!/usr/bin/env bash

if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[br_name]"
    exit
fi

br_name=$1

# Create bridge using OvS
echo "Deleting bridge $br_name"
ovs-vsctl del-br $br_name
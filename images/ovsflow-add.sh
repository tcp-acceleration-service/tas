#!/usr/bin/env bash

if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[br_name ip interface]"
    exit
fi

br_name=$1

# sudo ovs-ofctl -O OpenFlow10,OpenFlow11,OpenFlow12,OpenFlow13,OpenFlow14,OpenFlow15 add-flow br0 \
#   "table=0, priority=1, ip, in_port=rx_vtuoso, actions=set_tunnel:5, output:tx_vtuoso"
sudo ovs-ofctl add-flow br0 "table=0, priority=100, in_port=rx_vtuoso, actions=output:tx_vtuoso"
# ovs-ofctl -O OpenFlow10,OpenFlow11,OpenFlow12,OpenFlow13,OpenFlow14,OpenFlow15 \
#     add-flow $br_name "priority=1 in_port=2,actions=output:2"
#!/usr/bin/env bash

killall ovsdb-server ovs-vswitchd
rm -f /var/run/openvswitch/vhost-user*
rm -f /usr/local/etc/openvswitch/conf.db
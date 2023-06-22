#!/usr/bin/env bash

# Start database server
export DB_SOCK=/usr/local/var/run/openvswitch/db.sock
echo "Creating conf.db and ovsschema"
ovsdb-tool create /usr/local/etc/openvswitch/conf.db /usr/local/share/openvswitch/vswitch.ovsschema
echo "Starting serverver at ${DB_SOCK}"
ovsdb-server --remote=punix:${DB_SOCK} --remote=db:Open_vSwitch,Open_vSwitch,manager_options --pidfile --detach

# Start OVS
echo "Starting OVS"
ovs-vsctl --no-wait init
ovs-vsctl --no-wait set Open_vSwitch . other_config:dpdk-lcore-mask=0xf
ovs-vsctl --no-wait set Open_vSwitch . other_config:dpdk-socket-mem=1024
ovs-vsctl --no-wait set Open_vSwitch . other_config:dpdk-init=true

ovs-vswitchd unix:${DB_SOCK} --pidfile --detach --log-file=/usr/local/var/log/openvswitch/ovs-vswitchd.log
#!/bin/bash

sudo apt-get update -qq
sudo apt-get install -qq libnuma-dev libpcap-dev linux-headers-`uname -r`

instdir=${HOME}/dpdk/${DPDK_VER}

if [ ! -d $instdir ]
then
  # download dpdk source
  wget -O /tmp/dpdk.tar.xz http://fast.dpdk.org/rel/dpdk-${DPDK_VER}.tar.xz
  tar -x -f /tmp/dpdk.tar.xz -C ${HOME}
  rm -f /tmp/dpdk.tar.xz

  make -C ${HOME}/dpdk-* -j2 install \
    T=x86_64-native-linuxapp-gcc DESTDIR=$instdir
  rm -rf  ${HOME}/dpdk-*
fi


#!/usr/bin/env bash

set -ex

machineName=$1
option=$2
num=$3
cnt=0

STTY_SETTINGS="$( stty -g )"

stty intr ^]
stty susp ^]

cnt=$(($cnt+1))
# Parameters.
id=ubuntu-18.04.6-desktop-amd64
disk_img="base.img"
seed_img="seed.img"
disk_img_snapshot="${machineName}.snapshot.qcow2"

qemu-system-x86_64 \
  -nographic -monitor none -serial stdio \
  -machine accel=kvm,type=q35 \
  -cpu host \
  -smp 16 \
  -m 12G \
  -device virtio-net-pci,netdev=net0 \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 \
  -drive if=virtio,format=qcow2,file=${disk_img_snapshot} \
  -drive if=virtio,format=raw,file=${seed_img} \
  -chardev socket,path="/run/tasproxy",id="tas" \
  -device ivshmem-doorbell,vectors=1,chardev="tas"

stty "$STTY_SETTINGS"

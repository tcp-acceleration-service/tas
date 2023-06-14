#!/usr/bin/env bash

set -ex

stack=$1
vm_id=$2
interface=$3

stty intr ^]
stty susp ^]

STTY_SETTINGS="$( stty -g )"

# Parameters.
disk_img="base.img"
seed_img="seed.img"
disk_img_snapshot="base.snapshot.qcow2"
tap=tap$vm_id
tastap=tastap$vm_id
ovstap=ovstap$vm_id
vhost=vhost$vm_id

printf -v mac '02:00:00:%02X:%02X:%02X' $((RANDOM%256)) $((RANDOM%256)) $((RANDOM%256))
printf -v alt_mac '02:00:00:%02X:%02X:%02X' $((RANDOM%256)) $((RANDOM%256)) $((RANDOM%256))

echo $mac
echo $alt_mac

# Note: vectors=<2 + 2 * queues_nr>

if [[ "$stack" == 'virt-tas' ]]; then
  sudo qemu-system-x86_64 \
    -nographic -monitor none -serial stdio \
    -machine accel=kvm,type=q35 \
    -cpu host \
    -smp 12 \
    -m 25G \
    -snapshot \
    -device virtio-net-pci,netdev=net0 \
    -netdev user,id=net0,hostfwd=tcp::222${vm_id}-:22 \
    -chardev socket,path="/run/tasproxy",id="tas" \
    -device ivshmem-doorbell,vectors=1,chardev="tas" \
    -drive if=virtio,format=qcow2,file="base.snapshot.qcow2" \
    -drive if=virtio,format=raw,file="seed.img" \
    ;
elif [[ "$stack" == 'virt-linux' ]]; then
    sudo qemu-system-x86_64 \
      -nographic -monitor none -serial stdio \
      -machine accel=kvm,type=q35 \
      -cpu host \
      -smp 12 \
      -m 25G \
      -snapshot \
      -netdev user,id=net0 \
      -device virtio-net-pci,netdev=net0 \
      -netdev tap,ifname=$tap,script=no,downscript=no,vhost=on,id=net1 \
      -device virtio-net-pci,mac=$mac,netdev=net1 \
      -drive if=virtio,format=qcow2,file="base.snapshot.qcow2" \
      -drive if=virtio,format=raw,file="seed.img" \
      ;
elif [[ "$stack" == 'ovs-linux' ]]; then
  sudo qemu-system-x86_64 \
    -nographic -monitor none -serial stdio \
    -machine accel=kvm,type=q35 \
    -cpu host \
    -smp 12 \
    -m 25G \
    -snapshot \
    -netdev user,id=net0,hostfwd=tcp::222${vm_id}-:22 \
    -device virtio-net-pci,netdev=net0 \
    -chardev socket,id=char0,path=/usr/local/var/run/openvswitch/$vhost \
    -netdev type=vhost-user,chardev=char0,vhostforce=on,queues=12,id=net1 \
    -device virtio-net-pci,netdev=net1,mac=$alt_mac,mq=on,vectors=26 \
    -object memory-backend-file,id=mem,size=25G,mem-path=/dev/hugepages,share=on \
    -numa node,memdev=mem -mem-prealloc \
    -drive if=virtio,format=qcow2,file="base.snapshot.qcow2" \
    -drive if=virtio,format=raw,file="seed.img" \
    ;
elif [[ "$stack" == 'ovs-tas' ]]; then
  sudo qemu-system-x86_64 \
    -nographic -monitor none -serial stdio \
    -machine accel=kvm,type=q35 \
    -cpu host \
    -smp 12 \
    -m 25G \
    -snapshot \
    -netdev user,id=net0,hostfwd=tcp::222${vm_id}-:22 \
    -device virtio-net-pci,netdev=net0 \
    -chardev socket,id=char0,path=/usr/local/var/run/openvswitch/$vhost \
    -netdev type=vhost-user,chardev=char0,vhostforce=on,queues=12,id=net1 \
    -device virtio-net-pci,netdev=net1,mac=$alt_mac,mq=on,vectors=26 \
    -object memory-backend-file,id=mem,size=25G,mem-path=/dev/hugepages,share=on \
    -numa node,memdev=mem -mem-prealloc \
    -drive if=virtio,format=qcow2,file="base.snapshot.qcow2" \
    -drive if=virtio,format=raw,file="seed.img" \
    ;
elif [[ "$stack" == 'tap-tas' ]]; then
  sudo qemu-system-x86_64 \
    -nographic -monitor none -serial stdio \
    -machine accel=kvm,type=q35 \
    -cpu host \
    -smp 12 \
    -m 25G \
    -snapshot \
    -netdev user,id=net0 \
    -device virtio-net-pci,netdev=net0 \
    -netdev tap,ifname=$tap,script=no,downscript=no,vhost=on,id=net1 \
    -device virtio-net-pci,mac=$mac,netdev=net1 \
    -netdev tap,ifname=$tastap,script=no,downscript=no,vhost=on,queues=10,id=net2 \
    -device virtio-net-pci,mac=$alt_mac,vectors=18,mq=on,netdev=net2 \
    -drive if=virtio,format=qcow2,file="base.snapshot.qcow2" \
    -drive if=virtio,format=raw,file="seed.img" \
    ;
  elif [[ "$stack" == 'gre' ]]; then
  sudo qemu-system-x86_64 \
    -nographic -monitor none -serial stdio \
    -machine accel=kvm,type=q35 \
    -cpu host \
    -smp 12 \
    -m 25G \
    -snapshot \
    -netdev user,id=net0 \
    -device virtio-net-pci,netdev=net0 \
    -netdev bridge,br=br2,id=net1 \
    -device virtio-net-pci,id=nic1,netdev=net1 \
    -drive if=virtio,format=qcow2,file="base.snapshot.qcow2" \
    -drive if=virtio,format=raw,file="seed.img" \
    ;
fi

stty "$STTY_SETTINGS"

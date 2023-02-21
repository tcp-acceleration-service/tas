# Virtualized TCP Acceleration Service

[![Build Status](https://travis-ci.org/tcp-acceleration-service/tas.svg?branch=master)](https://travis-ci.org/tcp-acceleration-service/tas)
[![Documentation Status](https://readthedocs.org/projects/tas/badge/?version=latest)](https://tas.readthedocs.io/en/latest/?badge=latest)


vTAS is a drop-in highly CPU efficient and scalable TCP acceleration service for
virtualized environments.

## Building
Requirements:
  * vTAS is built on top of Intel DPDK for direct access to the NIC. We have
    tested this version with dpdk versions (17.11.9, 18.11.5, 19.11).

Assuming that dpdk is installed in `~/dpdk-inst` vTAS can be built as follows:
```
make RTE_SDK=~/dpdk-inst
```

This will build the vTAS service (binary `tas/tas`), client libraries (in
`lib/`), and a few debugging tools (in `tools/`).

## Running

Before running vTAS the following steps are necessary:
   * Make sure `hugetlbfs` is mounted on `/dev/hugepages` and enough huge pages are
     allocated for vTAS and dpdk.
   * Binding the NIC to the dpdk driver, as with any other dpdk application (for
     Intel NICs use `vfio` because `uio` does not support multiple interrupts).

```
sudo modprobe vfio-pci
sudo mount -t hugetlbfs nodev /dev/hugepages
echo 1024 | sudo tee /sys/devices/system/node/node*/hugepages/hugepages-2048kB/nr_hugepages
sudo ~/dpdk-inst/sbin/dpdk-devbind  -b vfio-pci 0000:08:00.0
```

To run vTAS you need to start 4 different components:
vTAS, the host proxy, a VM using QEMU's ivshmem and the guest proxy.

First start vTAS on the host with the following command:
```
sudo code/tas/tas --ip-addr=10.0.0.1/24 --fp-cores-max=1 --fp-no-ints --fp-no-autoscale --dpdk-extra="-w08:00.0"
```

After vTAS starts run the host proxy:
```
sudo code/proxy/host/host
```

With the host proxy up and running you can start QEMU with ivshmem. 
QEMU will grab the shared memory region opened by vTAS from the host proxy. 
You need a configured VM image before you can start QEMU. If you don't have one,
follow the steps in the [Building Images for QEMU](#building-images-for-qemu)
section. If you already have set up an image, start a VM with the command below:

```
sudo qemu-system-x86_64 \
  -nographic -monitor none -serial stdio \
  -machine accel=kvm,type=q35 \
  -cpu host \
  -smp 12 \
  -m 12G \
  -device virtio-net-pci,netdev=net0 \
  -netdev user,id=net0,hostfwd=tcp::2220-:22 \
  -chardev socket,path="/run/tasproxy",id="tas" \
  -device ivshmem-doorbell,vectors=1,chardev="tas" \
  -drive if=virtio,format=qcow2,file="base.snapshot.qcow2" \
  -drive if=virtio,format=raw,file="seed.img" \
  ;
```

After the VM boots, from another terminal window ssh into your VM.
Inside the VM start the guest proxy:
```
ssh -p 2222 tas@localhost
...login to VM
sudo code/tas/proxy/guest/guest
```

After the guest proxy is running you can run applications that use vTAS
inside the VM. Applications that directly link to `libtas` or
`libtas_sockets` can be run directly. To run an unmodified application with
sockets interposition run as follows (for example):
```
sudo LD_PRELOAD=lib/libtas_interpose.so ../benchmarks/micro_rpc/echoserver_linux 1234 1 foo 8192 1
```

### Kernel NIC Interface

TAS supports the DPDK kernel NIC interface (KNI) to pass packets to the Linux
kernel network stack. With KNI enabled, TAS becomes an opt-in fastpath where
TAS-enabled applications operate through TAS, and other applications can use the
Linux network stack as before, sharing the same physical NIC.

To run TAS with KNI the first step is to load the `rte_kni` kernel module. Next,
when run with the `--kni-name=` option, TAS will create a KNI dummy network
interface with the specified name. After assigning an IP address to this
network interface, the Linux network stack can send and receive packets through
this interface as long as TAS is running. Here is the complete sequence of
commands:

```
sudo modprobe rte_kni
sudo code/tas/tas --ip-addr=10.0.0.1/24 --kni-name=tas0
# in separate terminal
sudo ifconfig tas0 10.0.0.1/24 up
```

### Building Images for QEMU

You can find cloud images from the Ubuntu website. In this example we get
the cloud image for Ubuntu 20.04:
```
wget https://cloud-images.ubuntu.com/releases/focal/release/ubuntu-20.04-server-cloudimg-amd64.img
```

Resize the image to give you more disk space with the following command
```
qemu-img resize ubuntu-20.04-server-cloudimg-amd64.img +15G
```

After downloading an image, set it up by writing a user-data
and metadata that creates a user and configures your image.
Sample .yaml files that create a user named `tas` with password
`tas` can be found in the images directory. Afterwards use `cloud-localds`
to create a seed.img that sets up the initial config for your base image.

```
cloud-localds seed.img user-data.yaml metadata.yaml
```

Start your VM with the following command:

```
sudo qemu-system-x86_64 \
  -nographic -monitor none -serial stdio \
  -machine accel=kvm,type=q35 \
  -cpu host \
  -smp 16 \
  -m 12G \
  -device virtio-net-pci,netdev=net0 \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 \
  -drive if=virtio,format=qcow2,file=ubuntu-20.04-server-cloudimg-amd64.img \
  -drive if=virtio,format=raw,file=seed.img
```

You can then ssh to your vm from your local machine by executing:

```
ssh -p 2222 tas@localhost
```

We use vfio to access the shared memory region in the guest. It may be the
case that your image does not have noiommu enabled, so you need to enable it
and bind the ivshmem PCI device to the vfio-pci driver. So first get the device
vendor code and device code

```
$ lspci -n
00:00.0 0600: 8086:29c0
00:01.0 0300: 1234:1111 (rev 02)
00:02.0 0200: 1af4:1000
00:03.0 0500: 1af4:1110 (rev 01)
00:04.0 0100: 1af4:1001
00:05.0 0100: 1af4:1001
00:1f.0 0601: 8086:2918 (rev 02)
00:1f.2 0106: 8086:2922 (rev 02)
00:1f.3 0c05: 8086:2930 (rev 02)
```

In this example, the ivshmem device vendor code and device code is `1af4:1110`.
You can find the bus info for your device by running `lshw -class memory` and
looking for Inter-VM shared memory. The device in this example has id 00:03.0.

Now enable noiommu and bind the kernel driver to the PCI device. If vfio-pci is
not compiled with your kernel, you need to first load it as a module using
modprobe.

```
modprobe vfio_pci
echo 1 > /sys/module/vfio/parameters/enable_unsafe_noiommu_mode
echo 1af4 1110 > /sys/bus/pci/drivers/vfio-pci/new_id
```

To be able to run TAS applications in a VM you need to install DPDK inside
the VM. After DPDK is installed, clone the TAS repo and build it.

## Code Structure
  * `tas/`: service implementation
    * `tas/fast`: TAS fast path
    * `tas/slow`: TAS slow path
  * `proxy`: proxy implementation
    * `proxy/host`: proxy on host
    * `proxy/guest`: proxy on guest  
  * `lib/`: client libraries
    * `lib/tas`: lowlevel TAS client library (interface:
      `lib/tas/include/tas_ll.h`)
    * `lib/sockets`: socket emulation layer
  * `tools/`: debugging tools

# TCP Acceleration Service

[![Build Status](https://travis-ci.org/tcp-acceleration-service/tas.svg?branch=master)](https://travis-ci.org/tcp-acceleration-service/tas)

TAS is a drop-in highly CPU efficient and scalable TCP acceleration service.

Our [EuroSys2019 Paper](https://dl.acm.org/authorize?N678517) describes the TAS
design and its rationale.

## Building
Requirements:
  * TAS is built on top of Intel DPDK for direct access to the NIC. We have
    tested this version with dpdk-17.11.4

Assuming that dpdk is installed in `~/dpdk-inst` TAS can be built as follows:
```
make RTE_SDK=~/dpdk-inst
```

This will build the TAS service (binary `tas/tas`), client libraries (in
`lib/`), and a few debugging tools (in `tools/`).

## Running

Before running TAS the following steps are necessary:
   * Make sure `hugetlbfs` is mounted on `/mnt/huge` and enough huge pages are
     allocated for TAS and dpdk.
   * Binding the NIC to the dpdk driver, as with any other dpdk application (for
     Intel NICs use `vfio` because `uio` does not support multiple interrupts).

```
sudo modprobe vfio-pci
sudo mount -t hugetlbfs nodev /mnt/huge
echo 1024 | sudo tee /sys/devices/system/node/node*/hugepages/hugepages-2048kB/nr_hugepages
sudo ~/dpdk-inst/sbin/dpdk-devbind  -b vfio-pci 0000:08:00.0
```

To run (`--ip-addr` and `--fp-cores-max` are the minimum arguments typically
needed to run tas, for more see `--help`):
```
sudo code/tas/tas --ip-addr=10.0.0.1/24 --fp-cores-max=2
```

Once tas is running, applications that directly link to `libtas` or
`libtas_sockets` can be run directly. To run an unmodified application with
sockets interposition run as follows (for example):
```
sudo LD_PRELOAD=lib/libtas_interpose.so ../benchmarks/micro_rpc/echoserver_linux 1234 1 foo 8192 1
```

## Code Structure
  * `tas/`: service implementation
    * `tas/fast`: TAS fast path
    * `tas/slow`: TAS slow path
  * `lib/`: client libraries
    * `lib/tas`: lowlevel TAS client library (interface:
      `lib/tas/include/tas_ll.h`)
    * `lib/sockets`: socket emulation layer
  * `tools/`: debugging tools

#! /bin/bash
export VAI_UUID=`uuidgen`
echo $VAI_UUID | sudo tee /sys/class/fpga/intel-fpga-dev.0/intel-fpga-port.0/mdev_supported_types/intel-fpga-port-direct-0/create
echo 40000 | sudo tee /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages


sudo numactl -N 0 -m 0 /home/img/qemu-system-x86_64 \
    -enable-kvm \
    -smp 4 \
    -m 10000 \
    -mem-path /dev/hugepages \
    -mem-prealloc \
    -hda /home/lyq/workspace/img/vm0.qcow2 \
    -vnc :10 \
    -net user,hostfwd=tcp::2222-:22 \
    -net nic -monitor telnet:localhost:771,server,nowait \
    -device vfio-pci,sysfsdev=/sys/bus/mdev/devices/$VAI_UUID \
    -serial stdio
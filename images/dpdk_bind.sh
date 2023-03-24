set -o errexit

if [ "$#" -ne 3 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[ip, interface, pci_id]"
    exit
fi

# IP of vNIC
ip=$1
interface=$2
pci_id=$3

# Quit script if we are not in VM
user=`whoami`
if [ $user != 'tas' ]; then
    echo "exiting: user != tas means we are not in VM"
    exit 0
fi

# Bring interface down
sudo ip link set $interface down

# Bind vnic to dpdk
sudo modprobe vfio-pci
cd /home/tas/programs/dpdk-stable-22.11.3/usertools
sudo python3 dpdk-devbind.py -b vfio-pci $pci_id

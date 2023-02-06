import time

class VM:

    def __init__(self, defaults, machine_config, vm_config, wmanager):
        self.defaults = defaults
        self.machine_config = machine_config
        self.vm_config = vm_config
        self.wmanager = wmanager
        self.pane = self.wmanager.add_new_pane(vm_config.pane,
                machine_config.is_remote)
    
    def start(self):
        self.pane.send_keys('cd ' + self.vm_config.manager_dir)
        start_vm_cmd = "sudo bash start-vm.sh {} {}".format(
                self.machine_config.stack, self.vm_config.id)
        self.pane.send_keys(start_vm_cmd)
       
        print("Started VM")
        time.sleep(35)
        self.login_vm()

    def enable_hugepages(self):
        cmd = "sudo mount -t hugetlbfs nodev /dev/hugepages"
        self.pane.send_keys(cmd)
        cmd = "echo 1024 | sudo tee /sys/devices/system/node/node*/hugepages/hugepages-2048kB/nr_hugepages"
        self.pane.send_keys(cmd)
        time.sleep(3)

    def enable_noiommu(self, vendor_id):
        self.pane.send_keys("sudo su -")
        self.pane.send_keys("sudo echo 1 > /sys/module/vfio/parameters/enable_unsafe_noiommu_mode")
        self.pane.send_keys("sudo echo {} > /sys/bus/pci/drivers/vfio-pci/new_id".format(vendor_id))
        self.pane.send_keys("exit")
        time.sleep(1)

    def init_interface(self, ip, interface):    
        cmd = "sudo ip addr add {}/24 dev {}".format(ip, interface)
        self.pane.send_keys(cmd)
        time.sleep(1)
        cmd = "sudo ip link set {} up".format(interface)
        self.pane.send_keys(cmd)
        time.sleep(1)

    def dpdk_bind(self, ip, interface, pci_id):
        cmd = 'cd ' + self.vm_config.manager_dir_virt
        self.pane.send_keys(cmd)
        cmd = 'bash dpdk_bind.sh {} {} {}'.format(ip, interface, pci_id)
        self.pane.send_keys(cmd)
        time.sleep(3)

    def login_vm(self):
        self.pane.send_keys(suppress_history=False, cmd='tas')
        time.sleep(3)
        self.pane.send_keys(suppress_history=False, cmd='tas')
        self.pane.enter()
        time.sleep(5)
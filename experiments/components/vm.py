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
        start_vm_cmd = "sudo bash start-vm.sh {} {} {}".format(
                self.machine_config.stack, self.vm_config.id,
                self.machine_config.interface)
        self.pane.send_keys(start_vm_cmd)
       
        print("Started VM")
        time.sleep(25)
        self.login_vm()

    def enable_hugepages(self):
        cmd = "sudo mount -t hugetlbfs nodev /dev/hugepages"
        self.pane.send_keys(cmd)
        time.sleep(1)
        cmd = "echo 8192 | sudo tee /sys/devices/system/node/node*/hugepages/hugepages-2048kB/nr_hugepages"
        self.pane.send_keys(cmd)
        time.sleep(5)

    def enable_noiommu(self, vendor_id):
        self.pane.send_keys("sudo su -")
        time.sleep(1)
        self.pane.send_keys("sudo echo 1 > /sys/module/vfio/parameters/enable_unsafe_noiommu_mode")
        time.sleep(1)
        self.pane.send_keys("sudo echo {} > /sys/bus/pci/drivers/vfio-pci/new_id".format(vendor_id))
        time.sleep(1)
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
        cmd = 'bash dpdk-bind.sh {} {} {}'.format(ip, interface, pci_id)
        self.pane.send_keys(cmd)
        time.sleep(3)

    def login_vm(self):
        self.pane.send_keys(suppress_history=False, cmd='tas')
        time.sleep(3)
        self.pane.send_keys(suppress_history=False, cmd='tas')
        self.pane.enter()
        time.sleep(5)

    def add_dummy_intf(self, interface, ip, mac):
        cmd = 'cd ' + self.vm_config.manager_dir_virt
        self.pane.send_keys(cmd)
        cmd = "bash dummy-intf-add.sh {} {} {}".format(interface, ip, mac)
        self.pane.send_keys(cmd)
        time.sleep(1)

    def del_dummy_intf(self, interface, ip):
        cmd = 'cd ' + self.vm_config.manager_dir_virt
        self.pane.send_keys(cmd)
        cmd = "bash dummy-intf-del.sh {} {} {}".format(interface, ip)
        self.pane.send_keys(cmd)
        time.sleep(1)

    def shutdown(self):
        self.pane.send_keys(suppress_history=False, cmd='whoami')
        
        captured_pane = self.pane.capture_pane()
        user = captured_pane[len(captured_pane) - 2]
        
        # This means we are in the vm, so we don't 
        # accidentally shutdown machine
        if user == 'tas':
            self.pane.send_keys(suppress_history=False, cmd='sudo shutdown -h now')
            time.sleep(2)
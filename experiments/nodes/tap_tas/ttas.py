import time
import threading

from components.tas import TAS
from components.vm import VM
from nodes.node import Node

class TapTas(Node):
  
  def __init__(self, defaults, machine_config, tas_configs,
      vm_configs, wmanager, 
      setup_pane_name, cleanup_pane_name):

    Node.__init__(self, defaults, machine_config, wmanager, 
        setup_pane_name, cleanup_pane_name)
        
    self.tas_configs = tas_configs
    self.vm_configs = vm_configs
    self.vms = []
    self.tas = []

  def setup(self):
    super().setup()
    self.bridge_up(self.machine_config.ip, 
        self.machine_config.interface,
        self.vm_configs[0].manager_dir)

    for vm_config in self.vm_configs:
      self.tap_up("tap{}".format(vm_config.id), vm_config.manager_dir, 0)
      self.tap_up("tastap{}".format(vm_config.id), vm_config.manager_dir, 1)

  def cleanup(self):
    super().cleanup()
    self.bridge_down(self.machine_config.ip, 
        self.machine_config.interface, 
        self.vm_configs[0].manager_dir)

    for vm_config in self.vm_configs:
      self.tap_down("tap{}".format(vm_config.id), vm_config.manager_dir)
      self.tap_down("tastap{}".format(vm_config.id), vm_config.manager_dir)

    for vm in self.vms:
      vm.shutdown()

  def start_tas(self):
    for i in range(self.nodenum):
      tas_config = self.tas_configs[i]
      
      tas = TAS(defaults=self.defaults, 
        machine_config=self.machine_config, 
        tas_config=tas_config, 
        vm_config=self.vm_configs[i],
        wmanager=self.wmanager)
      
      self.tas.append(tas)
      tas.run_virt()
      time.sleep(3)

  def start_vm(self, vm, vm_config):
      vm.start()
      vm.enable_hugepages()
      vm.enable_noiommu("1af4 1110")
      vm.init_interface(vm_config.vm_ip, self.defaults.vm_interface)
      vm.init_interface(vm_config.tas_tap_ip, self.defaults.tas_interface)
      vm.dpdk_bind(vm_config.tas_tap_ip, self.defaults.tas_interface,
          self.defaults.pci_id)

  def start_vms(self):
    threads = []
    for vm_config in self.vm_configs:
      vm = VM(self.defaults, self.machine_config, vm_config, self.wmanager)
      self.vms.append(vm)
      vm_thread = threading.Thread(target=self.start_vm, args=(vm, vm_config))
      threads.append(vm_thread)
      vm_thread.start()

    for t in threads:
      t.join()
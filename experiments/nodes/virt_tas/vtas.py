import time

from components.vm import VM
from components.proxy import ProxyGuest
from nodes.node import Node

class VirtTas(Node):
  
  def __init__(self, defaults, machine_config, tas_config,
      proxyh_config, vm_configs, proxyg_configs,
      wmanager, setup_pane_name, cleanup_pane_name):

    Node.__init__(self, defaults, machine_config, wmanager, 
        setup_pane_name, cleanup_pane_name)
        
    self.tas_config = tas_config
    self.proxyh_config = proxyh_config
    self.vm_configs = vm_configs
    self.proxyg_configs = proxyg_configs
    self.vms = []

  def start_vms(self):
    for vm_config in self.vm_configs:
      vm = VM(self.defaults, self.machine_config, vm_config, self.wmanager)
      self.vms.append(vm)
      vm.start()
      vm.enable_noiommu("1af4 1110")

  def start_guest_proxies(self):
    for i in range(len(self.vm_configs)):
      proxyg_config = self.proxyg_configs[i]
      vm_config = self.vm_configs[i]
      proxyg = ProxyGuest(self.defaults, 
              self.machine_config,
              proxyg_config,
              vm_config,
              self.wmanager)
      proxyg.run()
      time.sleep(3)
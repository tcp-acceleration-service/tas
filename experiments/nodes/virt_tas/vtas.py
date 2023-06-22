import time
import threading

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

  def start_vm(self, vm):
    vm.start()
    vm.enable_noiommu("1af4 1110")
    vm.add_dummy_intf("eth0", vm.vm_config.vm_ip, "C8:D7:4A:4E:47:50")

  def cleanup(self):
    super().cleanup()
    self.ovsbr_del("br0")
    self.stop_ovs(self.vm_configs[0].manager_dir)
    for vm in self.vms:
      vm.del_dummy_intf("eth0", vm.vm_config.vm_ip)
      vm.shutdown()

  def start_vms(self):
    threads = []
    for vm_config in self.vm_configs:
      vm = VM(self.defaults, self.machine_config, vm_config, self.wmanager)
      self.vms.append(vm)
      vm_thread = threading.Thread(target=self.start_vm, args=(vm,))
      threads.append(vm_thread)
      vm_thread.start()

    for t in threads:
      t.join()

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
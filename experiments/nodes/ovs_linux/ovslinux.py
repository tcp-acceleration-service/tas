import time
import threading

from components.vm import VM
from nodes.node import Node

class OvsLinux(Node):
  
  def __init__(self, defaults, machine_config,
      vm_configs, wmanager, 
      setup_pane_name, cleanup_pane_name):

    Node.__init__(self, defaults, machine_config, wmanager, 
        setup_pane_name, cleanup_pane_name)
        
    self.vm_configs = vm_configs
    self.vms = []

  def setup(self):
    super().setup()
    self.ovs_make_install(self.defaults.original_ovs_path)
    self.start_ovs(self.vm_configs[0].manager_dir)
    self.ovsbr_add("br0", 
                   self.machine_config.ip + "/24", 
                   self.machine_config.interface,
                   self.vm_configs[0].manager_dir)
    
    for vm_config in self.vm_configs:
      # Tap that allows us to ssh to VM
      greid = vm_config.id + 1
      self.ovsvhost_add("br0", 
                        "vhost{}".format(vm_config.id),
                        "gre" + str(greid),
                        vm_config.vm_ip,
                        greid,
                        vm_config.manager_dir)

  def cleanup(self):
    super().cleanup()
    self.ovsbr_del("br0", self.vm_configs[0].manager_dir)
    self.stop_ovs(self.vm_configs[0].manager_dir)

    cmd = "sudo ip addr add {} dev {}".format(self.machine_config.ip + "/24",
                                              self.machine_config.interface)
    self.cleanup_pane.send_keys(cmd)
    time.sleep(1)

    cmd = "sudo ip link set dev {} up".format(self.machine_config.interface)
    self.cleanup_pane.send_keys(cmd)
    time.sleep(1)

    for vm in self.vms:
      vm.shutdown()
    
  def start_vm(self, vm, vm_config):
    vm.start()
    vm.init_interface(vm_config.vm_ip, self.defaults.vm_interface)
  
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

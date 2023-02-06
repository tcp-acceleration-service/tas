from components.vm import VM
from nodes.node import Node

class VirtLinux(Node):
  
  def __init__(self, defaults, machine_config,
      vm_configs, wmanager, 
      setup_pane_name, cleanup_pane_name):

    Node.__init__(self, defaults, machine_config, wmanager, 
        setup_pane_name, cleanup_pane_name)
        
    self.vm_configs = vm_configs
    self.vms = []

  def setup(self):
    super().setup()
    self.bridge_up(self.machine_config.ip, 
        self.machine_config.interface,
        self.vm_configs[0].manager_dir)

    for vm_config in self.vm_configs:
      self.tap_up("tap{}".format(vm_config.id), vm_config.manager_dir, 0)

  def cleanup(self):
    super().cleanup()
    self.bridge_down(self.machine_config.ip, 
        self.machine_config.interface, 
        self.vm_configs[0].manager_dir)

    for vm_config in self.vm_configs:
      self.tap_down("tap{}".format(vm_config.id), vm_config.manager_dir)

  def start_vms(self):
    for vm_config in self.vm_configs:
      vm = VM(self.defaults, self.machine_config, vm_config, self.wmanager)
      self.vms.append(vm)
      vm.start()
      vm.init_interface(vm_config.vm_ip, self.defaults.vm_interface)
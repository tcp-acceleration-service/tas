from components.vm import VM
from nodes.node import Node

class OVSLinux(Node):
  
  def __init__(self, defaults, machine_config,
      vm_configs, wmanager, 
      setup_pane_name, cleanup_pane_name):

    Node.__init__(self, defaults, machine_config, wmanager, 
        setup_pane_name, cleanup_pane_name)
        
    self.vm_configs = vm_configs
    self.vms = []

  def setup(self):
    super().setup()

    self.start_ovs(self.defaults.ovs_ctl_path)
    self.ovsbr_add("br0", 
                   self.machine_config.ip, 
                   self.machine_config.interface,
                   self.vm_configs[0].manager_dir)
    
    for vm_config in self.vm_configs:
      # Tap that allows us to ssh to VM
      self.ovstap_add("br0", 
                      "tap{}".format(vm_config.id), 
                      vm_config.manager_dir)
      # # TAP used by OvS
      # self.ovstap_add("br0", 
      #                 "ovstap{}".format(vm_config.id), 
      #                 vm_config.manager_dir)

  def cleanup(self):
    super().cleanup()
    self.ovsbr_del("br0")
    self.stop_ovs(self.defaults.ovs_ctl_path)

    for vm_config in self.vm_configs:
      self.tap_down("tap{}".format(vm_config.id), vm_config.manager_dir)
      # self.tap_down("ovstap{}".format(vm_config.id), vm_config.manager_dir)
    
  def start_vms(self):
    for vm_config in self.vm_configs:
      vm = VM(self.defaults, self.machine_config, vm_config, self.wmanager)
      self.vms.append(vm)
      vm.start()
      vm.init_interface(vm_config.vm_ip, self.defaults.vm_interface)
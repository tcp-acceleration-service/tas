import time

from nodes.ovs_tas.ovstas import OvsTas
from components.server import Server

class OvsTasServer(OvsTas):
  
  def __init__(self, config, wmanager):

    OvsTas.__init__(self, config.defaults, config.s_machine_config,
        config.s_vm_configs, wmanager, 
        config.defaults.s_setup_pane, 
        config.defaults.s_cleanup_pane)

    self.server_configs = config.server_configs
    self.nodenum = config.snodenum
    self.snum = config.snum
    self.server = []

  def start_servers(self):
    for i in range(self.nodenum):
      vm_config = self.vm_configs[i]
      for j in range(self.snum):
        sidx = self.snum * i + j
        server_config = self.server_configs[sidx]
        server = Server(self.defaults, 
                self.machine_config,
                server_config, 
                vm_config,
                self.wmanager)
        self.servers.append(server)
        server.run_virt(False, True)
        time.sleep(3)

  def run(self):
    self.setup()
    self.start_vms()
    self.start_tas()
    self.start_servers()
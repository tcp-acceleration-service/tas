import time

from nodes.tap_tas.ttas import TapTas
from components.server import Server

class TapTasServer(TapTas):
  
  def __init__(self, config, wmanager):

    TapTas.__init__(self, config.defaults, config.s_machine_config,
        config.s_tas_configs, config.s_vm_configs,
        wmanager, config.defaults.s_setup_pane, 
        config.defaults.s_cleanup_pane)

    self.server_configs = config.server_configs
    self.nodenum = config.snodenum
    self.snum = config.snum
    self.servers = []

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

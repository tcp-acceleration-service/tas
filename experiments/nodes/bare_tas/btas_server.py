import time

from nodes.bare_tas.btas import BareTas
from components.tas import TAS
from components.server import Server

class BareTasServer(BareTas):
  
  def __init__(self, config, wmanager):

    BareTas.__init__(self, config.defaults, config.s_machine_config,
        config.s_tas_configs[0], wmanager, 
        config.defaults.s_setup_pane, 
        config.defaults.s_cleanup_pane)

    self.server_configs = config.server_configs
    self.nodenum = config.snodenum
    self.snum = config.snum

  def start_servers(self):
    for server_config in self.server_configs:
      server = Server(self.defaults, 
              self.machine_config,
              server_config, 
              None,
              self.wmanager)
      server.run_bare(True, True)
      time.sleep(3)

  def run(self):
    self.setup()
    tas = TAS(self.defaults, self.machine_config, self.tas_config, self.wmanager)
    
    tas.run_bare()
    time.sleep(5)
    self.start_servers()

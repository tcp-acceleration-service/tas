import time

from nodes.vtas_bare.vtasbare import VTasBare
from components.tas import TAS
from components.server import Server

class VTasBareServer(VTasBare):
  
  def __init__(self, config, wmanager):

    VTasBare.__init__(self, config.defaults, config.s_machine_config,
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
      server.pane.send_keys("export TAS_GROUP={}".format(server_config.groupid))
      server.run_bare(True, True)
      time.sleep(3)

  def run(self):
    self.setup()
    tas = TAS(self.defaults, self.machine_config, self.tas_config, self.wmanager)
    
    tas.run_bare()
    time.sleep(5)
    self.start_servers()

import time

from nodes.bare_linux.blinux import BareLinux
from components.server import Server

class BareLinuxServer(BareLinux):
  
  def __init__(self, config, wmanager):

    BareLinux.__init__(self, config.defaults, config.s_machine_config,
        wmanager, config.defaults.s_setup_pane, 
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
      server.run_bare(False, False)
      time.sleep(3)

  def run(self):
    self.setup()
    self.start_servers()

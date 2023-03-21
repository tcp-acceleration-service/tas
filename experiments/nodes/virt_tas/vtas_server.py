import time

from nodes.virt_tas.vtas import VirtTas
from components.tas import TAS
from components.server import Server
from components.proxy import ProxyHost

class VirtTasServer(VirtTas):
  
  def __init__(self, config, wmanager):

    VirtTas.__init__(self, config.defaults, config.s_machine_config,
        config.s_tas_configs[0], config.s_proxyh_config, 
        config.s_vm_configs, config.s_proxyg_configs,
        wmanager, config.defaults.s_setup_pane, 
        config.defaults.s_cleanup_pane)

    self.server_configs = config.server_configs
    self.nodenum = config.snodenum
    self.snum = config.snum

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
        server.run_virt(True, True)
        time.sleep(3)

  def run(self):
    self.setup()
    tas = TAS(self.defaults, self.machine_config, self.tas_config, self.wmanager)
    proxyh = ProxyHost(self.defaults, self.machine_config, self.proxyh_config, self.wmanager)

    tas.run_bare()
    time.sleep(5)
    proxyh.run()
    time.sleep(3)

    self.start_vms()
    self.start_guest_proxies()
    self.start_servers()

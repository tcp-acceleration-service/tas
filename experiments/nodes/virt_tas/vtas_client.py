import time

from nodes.virt_tas.vtas import VirtTas
from components.tas import TAS
from components.proxy import ProxyHost
from components.client import Client

class VirtTasClient(VirtTas):
  
  def __init__(self, config, wmanager):

    VirtTas.__init__(self, config.defaults, config.c_machine_config,
        config.c_tas_configs[0], config.c_proxyh_config, 
        config.c_vm_configs, config.c_proxyg_configs,
        wmanager, config.defaults.c_setup_pane, 
        config.defaults.c_cleanup_pane)

    self.client_configs = config.client_configs
    self.nodenum = config.cnodenum
    self.cnum = config.cnum
    self.clients = []

  def start_clients(self):
    for i in range(self.nodenum):
      vm_config = self.vm_configs[i]
      for j in range(self.cnum):
        cidx = self.cnum * i + j
        client_config = self.client_configs[cidx]
        client = Client(self.defaults, 
            self.machine_config,
            client_config, 
            vm_config, 
            self.wmanager)
        self.clients.append(client)
        client.run_virt()
        time.sleep(3)

  def run(self):
    self.setup()
    tas = TAS(self.defaults, self.machine_config, 
        self.tas_config, self.wmanager)
    proxyh = ProxyHost(self.defaults, self.machine_config, 
        self.proxyh_config, self.wmanager)

    tas.run_bare()
    time.sleep(5)
    proxyh.run()
    time.sleep(3)

    self.start_vms()
    self.start_guest_proxies()
    self.start_clients()

  def save_logs(self, exp_path):
    for client in self.clients:
      client.save_log_virt(exp_path)

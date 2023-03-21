import time

from nodes.tap_tas.ttas import TapTas
from components.client import Client

class TapTasClient(TapTas):
  
  def __init__(self, config, wmanager):

    TapTas.__init__(self, config.defaults, config.c_machine_config,
        config.c_tas_configs, config.c_vm_configs,
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
        client.run_virt(False, True)
        time.sleep(3)

  def run(self):
    self.setup()
    self.start_vms()
    self.start_tas()
    self.start_clients()

  def save_logs(self, exp_path):
    for client in self.clients:
      client.save_log_virt(exp_path)

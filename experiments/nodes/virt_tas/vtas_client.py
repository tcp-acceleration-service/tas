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
    self.server_vm_configs = config.s_vm_configs
    self.clients = []

  def setup_tunnels(self):
    self.ovs_make_install(self.defaults.modified_ovs_path)
    self.start_ovs(self.vm_configs[0].manager_dir)
    self.ovsbr_add_vtuoso("br0", self.vm_configs[0].manager_dir)

    for i, vm_config in enumerate(self.vm_configs):
      rxport_name = "rx_vtuoso{}".format(vm_config.id)
      txport_name = "tx_vtuoso{}".format(vm_config.id)
      self.ovsport_add_vtuoso("br0", rxport_name, "virtuosorx", 
                              vm_config.id, 
                              vm_config.manager_dir)
      self.ovsport_add_vtuoso("br0", txport_name, "virtuosotx",
                              vm_config.id, 
                              vm_config.manager_dir,
                              out_remote_ip=self.defaults.server_ip, 
                              out_local_ip=self.defaults.client_ip,
                              in_remote_ip=self.server_vm_configs[i].vm_ip, 
                              in_local_ip=vm_config.vm_ip,
                              key=i + 1)
      self.ovsflow_add("br0", rxport_name, txport_name, vm_config.manager_dir)

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
        client.run_virt(True, True)
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
    self.setup_tunnels()
    self.start_vms()
    self.start_guest_proxies()
    self.start_clients()

  def save_logs(self, exp_path):
    for client in self.clients:
      client.save_log_virt(exp_path)

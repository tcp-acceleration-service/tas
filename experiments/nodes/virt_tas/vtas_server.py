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
    self.client_vm_configs = config.c_vm_configs
    self.snum = config.snum

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
                              out_remote_ip=self.defaults.client_ip, 
                              out_local_ip=self.defaults.server_ip,
                              in_remote_ip=self.client_vm_configs[i].vm_ip, 
                              in_local_ip=vm_config.vm_ip,
                              key=i + 1)
      self.ovsflow_add("br0", rxport_name, txport_name, vm_config.manager_dir)

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
    self.setup_tunnels()
    self.start_vms()
    self.start_guest_proxies()
    self.start_servers()

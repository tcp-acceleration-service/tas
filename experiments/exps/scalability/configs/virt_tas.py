from configs.gen_config import Defaults
from configs.gen_config import MachineConfig
from configs.gen_config import TasConfig
from configs.gen_config import VMConfig
from configs.gen_config import HostProxyConfig
from configs.gen_config import GuestProxyConfig
from configs.gen_config import ClientConfig
from configs.gen_config import ServerConfig

class Config:
    def __init__(self, exp_name, nconns):
        self.exp_name = exp_name
        self.defaults = Defaults()
        
        # Server Machine
        self.sstack = 'virt-tas'
        self.snum = 1
        self.snodenum = 1
        self.s_tas_configs = []
        self.s_vm_configs = []
        self.s_proxyg_configs = []
        self.server_configs = []
        
        self.s_machine_config = MachineConfig(ip=self.defaults.server_ip, 
                interface=self.defaults.server_interface,
                stack=self.sstack,
                is_remote=True,
                is_server=True)

        tas_config = TasConfig(pane=self.defaults.s_tas_pane,
                machine_config=self.s_machine_config,
                project_dir=self.defaults.default_vtas_dir_bare,
                ip=self.s_machine_config.ip,
                n_cores=12)
        self.s_tas_configs.append(tas_config)

        self.s_proxyh_config = HostProxyConfig(pane=self.defaults.s_proxyh_pane,
                machine_config=self.s_machine_config,
                comp_dir=self.defaults.default_vtas_dir_bare)
        
        for i in range(self.snodenum):
            vm_config = VMConfig(pane=self.defaults.s_vm_pane,
                    machine_config=self.s_machine_config,
                    tas_dir=self.defaults.default_vtas_dir_bare,
                    tas_dir_virt=self.defaults.default_vtas_dir_virt,
                    idx=i)
            self.s_vm_configs.append(vm_config)

            proxyg_config = GuestProxyConfig(pane=self.defaults.s_proxyg_pane,
                     machine_config=self.s_machine_config,
                     comp_dir=self.defaults.default_vtas_dir_virt)
            self.s_proxyg_configs.append(proxyg_config)

            for j in range(self.snum):
                server_config = ServerConfig(pane=self.defaults.s_server_pane,
                        idx=j, vmid=i,
                        port=1234, ncores=8, max_flows=1024, max_bytes=1024,
                        bench_dir=self.defaults.default_vbenchmark_dir_virt,
                        tas_dir=self.defaults.default_vtas_dir_virt)
                self.server_configs.append(server_config)

        # Client Machine
        self.cstack = 'virt-tas'
        self.cnum = 1
        self.cnodenum = 1
        self.c_tas_configs = []
        self.c_vm_configs = []
        self.c_proxyg_configs = []
        self.client_configs = []

        self.c_machine_config = MachineConfig(ip=self.defaults.client_ip, 
                interface=self.defaults.client_interface,
                stack=self.cstack,
                is_remote=False,
                is_server=False)

        tas_config = TasConfig(pane=self.defaults.c_tas_pane,
                machine_config=self.c_machine_config,
                project_dir=self.defaults.default_vtas_dir_bare,
                ip=self.c_machine_config.ip,
                n_cores=8)
        self.c_tas_configs.append(tas_config)

        self.c_proxyh_config = HostProxyConfig(pane=self.defaults.c_proxyh_pane,
                machine_config=self.c_machine_config,
                comp_dir=self.defaults.default_vtas_dir_bare)
        
        for i in range(self.cnodenum):
            vm_config = VMConfig(pane=self.defaults.c_vm_pane,
                    machine_config=self.c_machine_config,
                    tas_dir=self.defaults.default_vtas_dir_bare,
                    tas_dir_virt=self.defaults.default_vtas_dir_virt,
                    idx=i)
            self.c_vm_configs.append(vm_config)

            proxyg_config = GuestProxyConfig(pane=self.defaults.c_proxyg_pane,
                    machine_config=self.c_machine_config,
                    comp_dir=self.defaults.default_vtas_dir_virt)
            self.c_proxyg_configs.append(proxyg_config)

            for j in range(self.cnum):
                client_config = ClientConfig(exp_name=exp_name, 
                        pane=self.defaults.c_client_pane,
                        idx=j, vmid=i, stack=self.cstack,
                        ip=self.defaults.server_ip, port=1234, ncores=8,
                        msize=64, mpending=64, nconns=nconns,
                        open_delay=15, max_msgs_conn=0, max_pend_conns=1,
                        bench_dir=self.defaults.default_vbenchmark_dir_virt,
                        tas_dir=self.defaults.default_vtas_dir_virt)
                self.client_configs.append(client_config)
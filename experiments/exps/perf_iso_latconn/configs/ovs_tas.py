from configs.gen_config import Defaults
from configs.gen_config import MachineConfig
from configs.gen_config import TasConfig
from configs.gen_config import VMConfig
from configs.gen_config import ClientConfig
from configs.gen_config import ServerConfig

class Config:
    def __init__(self, exp_name, nconns):
        self.exp_name = exp_name
        self.defaults = Defaults()
        
        # Server Machine
        self.sstack = 'ovs-tas'
        self.snum = 1
        self.snodenum = 2
        self.s_tas_configs = []
        self.s_vm_configs = []
        self.s_proxyg_configs = []
        self.server_configs = []
        
        self.s_machine_config = MachineConfig(ip=self.defaults.server_ip, 
                interface=self.defaults.server_interface,
                stack=self.sstack,
                is_remote=True,
                is_server=True)

        vm0_config = VMConfig(pane=self.defaults.s_vm_pane,
                machine_config=self.s_machine_config,
                tas_dir=self.defaults.default_vtas_dir_bare,
                tas_dir_virt=self.defaults.default_vtas_dir_virt,
                idx=0,
                n_cores=11,
                memory=10,
                n_queues=11)
        tas0_config = TasConfig(pane=self.defaults.s_tas_pane,
                machine_config=self.s_machine_config,
                project_dir=self.defaults.default_otas_dir_virt,
                ip=vm0_config.vm_ip,
                n_cores=5, dpdk_extra="00:03.0")
        tas0_config.args = tas0_config.args + " --fp-no-xsumoffload --fp-no-rss"
        

        vm1_config = VMConfig(pane=self.defaults.s_vm_pane,
                machine_config=self.s_machine_config,
                tas_dir=self.defaults.default_vtas_dir_bare,
                tas_dir_virt=self.defaults.default_vtas_dir_virt,
                idx=1,
                n_cores=11,
                memory=10,
                n_queues=11)
        tas1_config = TasConfig(pane=self.defaults.s_tas_pane,
                machine_config=self.s_machine_config,
                project_dir=self.defaults.default_otas_dir_virt,
                ip=vm1_config.vm_ip,
                n_cores=5, dpdk_extra="00:03.0")
        tas1_config.args = tas1_config.args + " --fp-no-xsumoffload --fp-no-rss"


        self.s_tas_configs.append(tas0_config)
        self.s_tas_configs.append(tas1_config)
        self.s_vm_configs.append(vm0_config)
        self.s_vm_configs.append(vm1_config)

        server0_config = ServerConfig(pane=self.defaults.s_server_pane,
                idx=0, vmid=0,
                port=1234, ncores=5, max_flows=8192, max_bytes=4096,
                bench_dir=self.defaults.default_obenchmark_dir_virt,
                tas_dir=self.defaults.default_otas_dir_virt)
        server1_config = ServerConfig(pane=self.defaults.s_server_pane,
                idx=1, vmid=1,
                port=1235, ncores=5, max_flows=8192, max_bytes=4096,
                bench_dir=self.defaults.default_obenchmark_dir_virt,
                tas_dir=self.defaults.default_otas_dir_virt)
        self.server_configs.append(server0_config)
        self.server_configs.append(server1_config)

        # Client Machine
        self.cstack = 'ovs-tas'
        self.cnum = 1
        self.cnodenum = 2
        self.c_tas_configs = []
        self.c_vm_configs = []
        self.c_proxyg_configs = []
        self.client_configs = []

        self.c_machine_config = MachineConfig(ip=self.defaults.client_ip, 
                interface=self.defaults.client_interface,
                stack=self.cstack,
                is_remote=False,
                is_server=False)

        
        vm0_config = VMConfig(pane=self.defaults.c_vm_pane,
                machine_config=self.c_machine_config,
                tas_dir=self.defaults.default_vtas_dir_bare,
                tas_dir_virt=self.defaults.default_vtas_dir_virt,
                idx=0,
                n_cores=11,
                memory=10,
                n_queues=11)
        tas0_config = TasConfig(pane=self.defaults.c_tas_pane,
                machine_config=self.c_machine_config,
                project_dir=self.defaults.default_otas_dir_virt,
                ip=vm0_config.vm_ip,
                n_cores=1, dpdk_extra="00:03.0")
        tas0_config.args = tas0_config.args + " --fp-no-xsumoffload --fp-no-rss"
        

        vm1_config = VMConfig(pane=self.defaults.c_vm_pane,
                machine_config=self.c_machine_config,
                tas_dir=self.defaults.default_vtas_dir_bare,
                tas_dir_virt=self.defaults.default_vtas_dir_virt,
                idx=1,
                n_cores=11,
                memory=10,
                n_queues=11)
        tas1_config = TasConfig(pane=self.defaults.c_tas_pane,
                machine_config=self.c_machine_config,
                project_dir=self.defaults.default_otas_dir_virt,
                ip=vm1_config.vm_ip,
                n_cores=1, dpdk_extra="00:03.0")
        tas1_config.args = tas1_config.args + " --fp-no-xsumoffload --fp-no-rss"


        self.c_tas_configs.append(tas0_config)
        self.c_tas_configs.append(tas1_config)
        self.c_vm_configs.append(vm0_config)
        self.c_vm_configs.append(vm1_config)

        client0_config = ClientConfig(exp_name=exp_name, 
                pane=self.defaults.c_client_pane,
                idx=0, vmid=0, stack=self.cstack,
                ip=self.s_vm_configs[0].vm_ip, port=1234, ncores=1,
                msize=64, mpending=64, nconns=1,
                open_delay=15, max_msgs_conn=0, max_pend_conns=1,
                bench_dir=self.defaults.default_obenchmark_dir_virt,
                tas_dir=self.defaults.default_otas_dir_virt)
        client1_config = ClientConfig(exp_name=exp_name, 
                pane=self.defaults.c_client_pane,
                idx=0, vmid=1, stack=self.cstack,
                ip=self.s_vm_configs[1].vm_ip, port=1235, ncores=3,
                msize=64, mpending=64, nconns=nconns,
                open_delay=15, max_msgs_conn=0, max_pend_conns=1,
                bench_dir=self.defaults.default_obenchmark_dir_virt,
                tas_dir=self.defaults.default_otas_dir_virt)

        self.client_configs.append(client0_config)
        self.client_configs.append(client1_config)
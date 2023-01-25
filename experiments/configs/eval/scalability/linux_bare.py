from configs.gen_config import HostConfig

class Config:
    def __init__(self, n_conns):
        self.pane_prefix = 'e_'
        self.remote_connect_cmd = 'ssh swsnetlab04'
        server_ip = "192.168.10.14"

        self.server = HostConfig(
            name='server',
            is_server=True,
            is_remote=True,
            is_virt=False)

        self.client = HostConfig(
            name='client',
            is_server=False,
            is_remote=False,
            is_virt=False)

        self.snode = HostConfig(
            name='node',
            is_server=True,
            is_remote=True,
            is_virt=False)

        self.cnode = HostConfig(
            name='node',
            is_server=False,
            is_remote=False,
            is_virt=False)

        self.stype = 'bare'
        self.sstack = 'linux'
        self.snum = 1
        self.snodenum = 1
        self.ctype = 'bare'
        self.cstack = 'linux'
        self.cnum = 1
        self.cnodenum = 1
        self.msize = 64

        self.benchmark_server_args = "1234 8 foo 1024 1024"
        
        c_args0 = "{} 1234 8 foo {} 64 {} 30 0 1".format(
                server_ip, self.msize, n_conns)
        self.benchmark_client_args = [c_args0]
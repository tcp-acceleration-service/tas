from configs.gen_config import HostConfig

class Config:
    def __init__(self):
        self.pane_prefix = 'e_'
        self.remote_connect_cmd = 'ssh swsnetlab04'

        self.server = HostConfig(
            name='server',
            is_server=True,
            is_remote=True,
            is_virt=True)

        self.client = HostConfig(
            name='client',
            is_server=False,
            is_remote=False,
            is_virt=True)

        self.snode = HostConfig(
            name='node',
            is_server=True,
            is_remote=True,
            is_virt=True)

        self.cnode = HostConfig(
            name='node',
            is_server=False,
            is_remote=False,
            is_virt=True)

        self.stype = 'virt'
        self.sstack = 'tas'
        self.snum = 1
        self.ctype = 'virt'
        self.cstack = 'tas'
        self.cnum = 1
        self.connum = 1
        self.msize = 64

        # Setup and clean up of tap device
        self.server.setup_cmds = ["sudo brctl add br0",
                "sudo ip addr flush dev ens1f0",
                "sudo brctl addif br0 ens1f0",
                "sudo tunctl -t tap0 -u `whoami`",
                "sudo brctl addif br0 tap0",
                "sudo ifconfig ens1f0 up",
                "sudo ifconfig tap0 up",
                "sudo ifconfig br0 up"
                "sudo ip addr add 192.168.10.14/24 dev ens1f0"]
        self.server.cleanup_cmds = ["sudo brctl delif br0 tap0",
                "sudo tunctl -d tap0",
                "sudo brctl delif br0 ens1f0",
                "sudo ifconfig br0 down",
                "sudo brctl delbr br0",
                "sudo ifonfig ens1f0 up",
                "sudo ip addr add 192.168.10.14/24 dev ens1f0"]

        self.client.setup_cmds = ["sudo brctl add br0",
                "sudo ip addr flush dev ens1f0np0",
                "sudo brctl addif br0 ens1f0np0",
                "sudo tunctl -t tap0 -u `whoami`",
                "sudo brctl addif br0 tap0",
                "sudo ifconfig ens1f0np0 up",
                "sudo ifconfig tap0 up",
                "sudo ifconfig br0 up"
                "sudo ip addr add 192.168.10.13/24 dev ens1f0np0"]
        self.client.cleanup_cmds = ["sudo brctl delif br0 tap0",
                "sudo tunctl -d tap0",
                "sudo brctl delif br0 ens1f0np0",
                "sudo ifconfig br0 down",
                "sudo brctl delbr br0",
                "sudo ifonfig ens1f0np0 up",
                "sudo ip addr add 192.168.10.14/24 dev ens1f0np0"]

        self.benchmark_server_args = " 1234 1 foo 4096 1024"
        self.benchmark_client_args = " 192.168.10.14 1234 1 foo " + \
                str(self.msize) + " 64 " + str(self.connum) + " 0 0 16"
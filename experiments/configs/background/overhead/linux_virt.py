from configs.gen_config import HostConfig

def setup_tap(num, intf, ip):
    setup_cmds = ["sudo brctl addbr br0",
            "sudo ip addr flush dev {}".format(intf),
            "sudo brctl addif br0 {}".format(intf)]

    for i in range(num):
        setup_cmds.append("sudo tunctl -t tap{} -u `whoami`".format(i))
        setup_cmds.append("sudo brctl addif br0 tap{}".format(i))
        setup_cmds.append("sudo ifconfig tap{} up".format(i))

    setup_cmds.append("sudo ifconfig {} up".format(intf))
    setup_cmds.append("sudo ifconfig br0 up")
    setup_cmds.append("sudo ip addr add {}/24 dev br0".format(ip))

    return setup_cmds
    

def cleanup_tap(num, intf, ip):
    cleanup_cmds = []

    for i in range(num):
        cleanup_cmds.append("sudo brctl delif br0 tap{}".format(i))
        cleanup_cmds.append("sudo tunctl -d tap{}".format(i))

    cleanup_cmds.append("sudo brctl delif br0 {}".format(intf))
    cleanup_cmds.append("sudo ifconfig br0 down")
    cleanup_cmds.append("sudo brctl delbr br0")
    cleanup_cmds.append("sudo ifonfig {} up".format(intf))
    cleanup_cmds.append("sudo ip addr add {}/24 dev {}".format(ip, intf))

    return cleanup_cmds

class Config:
    def __init__(self):
        self.pane_prefix = 'e_'
        self.remote_connect_cmd = 'ssh swsnetlab04'
        server_ip = "192.168.10.14"
        client_ip = "192.168.10.13"

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
        self.sstack = 'linux'
        self.snum = 1
        self.ctype = 'virt'
        self.cstack = 'linux'
        self.cnum = 1
        self.connum = 1
        self.msize = 64

        # Setup and clean up of tap device
        self.server.setup_cmds = setup_tap(self.snum, "ens1f0", server_ip)
        self.server.cleanup_cmds = cleanup_tap(self.snum, "ens1f0", server_ip)

        self.client.setup_cmds = setup_tap(self.cnum, "ens1f0np0", client_ip)
        self.client.cleanup_cmds = cleanup_tap(self.cnum, "ens1f0np0", client_ip)

        self.benchmark_server_args = "1234 1 foo 4096 1024"
        self.benchmark_client_args = "{} 1234 1 foo {} 64 {} 0 0 16".format(
                server_ip, self.msize, self.connum)
import utils
import time

class Proxy:

    def __init__(self, defaults, machine_config, proxy_config, wmanager):
        self.defaults = defaults
        self.machine_config = machine_config
        self.proxy_config = proxy_config
        self.wmanager = wmanager
        self.pane = self.wmanager.add_new_pane(proxy_config.pane,
                machine_config.is_remote)

class ProxyHost(Proxy):

    def __init__(self, defaults, machine_config, proxy_config, wmanager):
        Proxy.__init__(self, defaults, machine_config, proxy_config, wmanager)
    
    def run(self):
        self.pane.send_keys('sudo rm ' + self.proxy_config.ivshm_socket_path)
        utils.compile_and_run(
                pane=self.pane,
                comp_dir=self.proxy_config.comp_dir,
                comp_cmd=self.proxy_config.comp_cmd,
                clean_cmd=self.proxy_config.clean_cmd,
                exec_file=self.proxy_config.exec_file,
                out=self.proxy_config.out,
                args='',
                clean=False)

class ProxyGuest(Proxy):
    
    def __init__(self, defaults, machine_config, proxy_config, vm_config, wmanager):
        Proxy.__init__(self, defaults, machine_config, proxy_config, wmanager)
        self.vm_config = vm_config
        self.wmanager = wmanager

    def run(self):
        ssh_com = utils.get_ssh_command(self.machine_config, self.vm_config)
        self.pane.send_keys(ssh_com)
        time.sleep(3)
        self.pane.send_keys("tas")
        time.sleep(2)

        utils.compile_and_run(
                pane=self.pane,
                comp_dir=self.proxy_config.comp_dir,
                comp_cmd=self.proxy_config.comp_cmd,
                clean_cmd=self.proxy_config.clean_cmd,
                exec_file=self.proxy_config.exec_file,
                out=self.proxy_config.out,
                args='',
                bg=False,
                clean=False)
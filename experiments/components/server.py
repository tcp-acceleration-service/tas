import utils
import time

class Server:
    
    def __init__(self, defaults, machine_config, 
            server_config, vm_config, wmanager):
        self.defaults = defaults
        self.machine_config = machine_config
        self.server_config = server_config
        self.vm_config = vm_config
        self.wmanager = wmanager
        self.log_paths = []
        self.pane = self.wmanager.add_new_pane(server_config.pane,
            machine_config.is_remote)
    
    def run_bare(self):
        self.run_benchmark_rpc()

    def run_virt(self):
        ssh_com = utils.get_ssh_command(self.machine_config, self.vm_config)
        self.pane.send_keys(ssh_com)
        time.sleep(3)
        self.pane.send_keys("tas")
        self.run_benchmark_rpc()

    def run_benchmark_rpc(self):
        self.pane.send_keys('cd ' + self.server_config.comp_dir)
        self.pane.send_keys(self.server_config.comp_cmd)
        self.pane.send_keys("cd " + self.server_config.tas_dir)
        time.sleep(3)

        cmd = 'sudo '
        stack = self.machine_config.stack
        if stack == 'bare-tas' or stack == 'tap-tas' or stack == 'virt-tas':
            cmd += 'LD_PRELOAD=' + self.server_config.lib_so + ' '
       
        cmd += self.server_config.exec_file + ' ' + \
                self.server_config.args
                #  + \
                # ' | tee ' + \
                # self.server_config.out
    
        print(cmd) 
        self.pane.send_keys(cmd)
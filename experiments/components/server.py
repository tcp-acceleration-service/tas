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
    
    def run_bare(self, w_sudo, ld_preload):
        cores = "22,24,26,28,30,32,34,36,38,40,42"
        self.run_benchmark_rpc(w_sudo, ld_preload, clean=False, cores=cores)

    def run_virt(self, w_sudo, ld_preload):
        ssh_com = utils.get_ssh_command(self.machine_config, self.vm_config)
        self.pane.send_keys(ssh_com)
        time.sleep(3)
        self.pane.send_keys("tas")
        self.run_benchmark_rpc(w_sudo, ld_preload, clean=False)

    def run_benchmark_rpc(self, w_sudo, ld_preload, clean, cores=None):
        self.pane.send_keys('cd ' + self.server_config.comp_dir)

        if clean:
            self.pane.send_keys(self.server_config.clean_cmd)
            time.sleep(1)

        self.pane.send_keys(self.server_config.comp_cmd)
        time.sleep(1)
        self.pane.send_keys("cd " + self.server_config.tas_dir)
        time.sleep(3)

        cmd = ''
        stack = self.machine_config.stack
        
        # Keep application on even cores, so it's the same NUMA node as TAS
        if cores is not None:
            cmd += "taskset -c {} ".format(cores)

        if w_sudo:
            cmd = 'sudo -E '
        
        if ld_preload:
            cmd += 'LD_PRELOAD=' + self.server_config.lib_so + ' '
       
        cmd += self.server_config.exec_file + ' ' + \
                self.server_config.args
                #  + \
                # ' | tee ' + \
                # self.server_config.out
    
        self.pane.send_keys(cmd)
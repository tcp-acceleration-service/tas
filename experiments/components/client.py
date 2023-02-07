import time
import os

import utils

class Client:
    
    def __init__(self, defaults, machine_config, client_config, vm_config, wmanager):
        self.defaults = defaults
        self.machine_config = machine_config
        self.client_config = client_config
        self.vm_config = vm_config
        self.wmanager = wmanager
        self.pane = self.wmanager.add_new_pane(client_config.pane, 
                machine_config.is_remote)
        self.save_logs_pane = self.wmanager.add_new_pane(defaults.c_savelogs_pane,
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
        self.pane.send_keys('cd ' + self.client_config.comp_dir)
        self.pane.send_keys(self.client_config.comp_cmd)
        self.pane.send_keys("cd " + self.client_config.tas_dir)
        time.sleep(3)

        cmd = ''
        stack = self.client_config.stack
        if stack == 'bare-tas' or stack == 'tap-tas' or stack == 'virt-tas' or stack == 'bare-vtas':
            cmd += 'LD_PRELOAD=' + self.client_config.lib_so + ' '
       
        cmd += self.client_config.exec_file + ' ' + \
                self.client_config.args + \
                ' | tee ' + \
                self.client_config.out
    
        print(cmd) 
        self.pane.send_keys(cmd)
    
    def save_log_virt(self, exp_path):
        split_path = exp_path.split("/")
        n = len(split_path)
        
        out_dir = os.getcwd() + "/" + "/".join(split_path[:n - 1]) + "/out"
        if not os.path.exists(out_dir):
            os.makedirs(out_dir)

        scp_com = utils.get_scp_command(self.machine_config, self.vm_config,
                self.client_config.out,
                out_dir + '/' + self.client_config.out_file)
        self.save_logs_pane.send_keys(scp_com)
        time.sleep(3)
        self.save_logs_pane.send_keys(suppress_history=False, cmd='tas')
        time.sleep(1)

    def save_log_bare(self, exp_path):
        # self.exp_path is set in the run.py file
        split_path = exp_path.split("/")
        n = len(split_path)
        
        out_dir = os.getcwd() + "/" + "/".join(split_path[:n - 1]) + "/out"
        if not os.path.exists(out_dir):
            os.makedirs(out_dir)

        dest = out_dir + "/" + self.client_config.out_file
        os.rename(self.client_config.out, dest)
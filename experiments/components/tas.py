import utils
import time
import os

class TAS:
    
    def __init__(self, defaults, machine_config, 
            tas_config, wmanager, vm_config=None):
        self.defaults = defaults
        self.machine_config = machine_config
        self.tas_config = tas_config
        self.vm_config = vm_config
        self.wmanager = wmanager
        self.pane = self.wmanager.add_new_pane(tas_config.pane, 
                machine_config.is_remote)

    def run_bare(self):
        tas_args = self.tas_config.args
        utils.compile_and_run(pane=self.pane,
                comp_dir=self.tas_config.comp_dir,
                comp_cmd=self.tas_config.comp_cmd,
                exec_file=self.tas_config.exec_file,
                out=self.tas_config.out,
                args=tas_args)

    def run_virt(self):
        ssh_com = utils.get_ssh_command(self.machine_config, self.vm_config)
        self.pane.send_keys(ssh_com)
        time.sleep(3)
        self.pane.send_keys("tas")
        
        tas_args = self.tas_config.args
        utils.compile_and_run(pane=self.pane,
                comp_dir=self.tas_config.comp_dir,
                comp_cmd=self.tas_config.comp_cmd,
                exec_file=self.tas_config.exec_file,
                out=self.tas_config.out,
                args=tas_args)

    def save_log_bare(self, exp_path):
        split_path = exp_path.split("/")
        n = len(split_path)
        
        out_dir = os.getcwd() + "/" + "/".join(split_path[:n - 1]) + "/out"
        if not os.path.exists(out_dir):
            os.makedirs(out_dir)

        dest = out_dir + "/" + self.tas_config.out_file
        os.rename(self.tas_config.out, dest)
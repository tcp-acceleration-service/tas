import libtmux
import time
import os

class Host(object):

    def __init__(self, wmanager, gen_config, config, nconfig,
            htype, hstack, hnum, log_paths):
        self.htype = htype
        self.hstack = hstack
        self.wmanager = wmanager
        self.gen_config = gen_config
        self.config = config
        self.node_config = nconfig
        self.node_num = hnum
        self.log_paths = log_paths

    def run_tas(self):
        pane = self.wmanager.add_new_pane(self.config.tas_pane, 
                self.config.is_remote)

        pane.send_keys("ulimit -c unlimited")
        Host.compile_and_run(
                pane=pane,
                comp_dir=self.config.tas_comp_dir,
                comp_cmd=self.config.tas_comp_cmd,
                exec_file=self.config.tas_exec_file,
                out_file=self.config.tas_out_file,
                args=self.config.tas_args,
                gdb=False, is_tas=False)
    
    def run_setup_cmds(self):
        pane = self.wmanager.add_new_pane(self.config.setup_pane,
                self.config.is_remote)
        time.sleep(3)
        for cmd in self.config.setup_cmds:
            pane.send_keys(cmd)
            time.sleep(1)
        self.setup_pane = pane

    def run_cleanup_cmds(self):
        pane = self.wmanager.add_new_pane(self.config.cleanup_pane,
                self.config.is_remote)

        for cmd in self.config.cleanup_cmds:
            pane.send_keys(cmd)
            time.sleep(1)
        self.cleanup_pane = pane


    def run_host_proxy(self):
        pane = self.wmanager.add_new_pane(self.config.proxy_pane,
                self.config.is_remote)
        pane.send_keys('sudo rm ' + self.config.proxy_ivshm_socket_path)
        Host.compile_and_run(
                pane=pane,
                comp_dir=self.config.host_proxy_comp_dir,
                comp_cmd=self.config.host_proxy_comp_cmd,
                exec_file=self.config.host_proxy_exec_file,
                out_file=self.config.host_proxy_out_file,
                args='', gdb=False, is_tas=False)

    def run_vms(self, num, exp):
        window_name = self.config.node_pane
        pane = self.wmanager.add_new_pane(window_name, 
                self.config.is_remote)
        self.run_vm(
                pane=pane, 
                window_name=window_name + str(num), 
                exp=exp,
                num=num)

    def run_benchmark(self, num, exp):
        print("benchmark", end = " ")
        if self.config.is_server:
            print("server :", end = " ")
        else :
            print("client :", end = " ")
        pane = self.wmanager.add_new_pane(self.config.benchmark_pane, 
                self.config.is_remote)

        if self.node_config.is_server:
            benchmark_args = self.gen_config.benchmark_server_args
        else:
            benchmark_args = self.gen_config.benchmark_client_args[num]
 
        self.run_benchmark_rpc(
                pane = pane,
                stack = self.hstack,
                comp_dir = self.config.benchmark_comp_dir,
                comp_cmd = self.config.benchmark_comp_cmd,
                lib_so = self.config.tas_lib_so,
                exec_file = self.config.benchmark_exec_file,
                out = self.config.benchmark_out + '_' + exp + '_' + str(num),
                args=benchmark_args)

    @staticmethod
    def compile_and_run(pane, comp_dir, comp_cmd, exec_file, out_file, args, 
            bg=False, gdb=False, is_tas=False):
        pane.send_keys('cd ' + comp_dir)
        pane.send_keys('git pull')
        time.sleep(1)
        pane.send_keys(comp_cmd)
        if gdb:
            cmd = 'sudo gdb --args ' + exec_file + ' ' + args
        else:
            cmd = 'sudo ' + exec_file + ' ' + args

        if bg : 
            cmd += ' &  '
        # cmd += ' # | tee ' + out_file
        pane.send_keys(cmd)

        if is_tas:
            pane.send_keys("break tcp.c:133")

        if gdb:
            pane.send_keys("run")

    def run(self, exp):
        self.run_setup_cmds()
        if self.hstack == 'tas':
            self.run_tas()
            time.sleep(2)
        if self.hstack == 'tas' and self.htype == 'virt':
            self.run_host_proxy()
            time.sleep(2)
        for i in range(self.node_num):
            if self.htype == 'virt':
                self.run_vms(i, exp=exp)
                self.run_guest_proxy(i)
                self.run_guest_benchmark(exp, i)
            else:
                self.run_benchmark(num=i, exp=exp)

        print()

    def login_vm(self, pane, window_name):
        print("logging in. ("+ window_name + ")")
        pane.enter()
        pane.send_keys(suppress_history=False, cmd='tas')
        time.sleep(3)
        pane.send_keys(suppress_history=False, cmd='tas')
        pane.enter()
        time.sleep(3)

    def run_vm(self, pane, window_name, exp, num = 0):
        """ Run preboot commands """
        for cmd in self.config.vm_manager_preboot_cmds:
            pane.send_keys(cmd(num))
            time.sleep(2)

        """ Start VM using VM-manager.sh """
        pane.send_keys('cd ' + self.config.vm_manager_dir)

        cmd = 'sudo '

        # Can't set one's place bit or else MAC address won't be valid
        if self.config.is_server:
            mac = 10 + num * 2
        else:
            mac = 30 + num * 2
        
        if self.hstack == 'linux':
            cmd += self.config.vm_manager('base', 'tap', num, mac)
        else:
            cmd += self.config.vm_manager('base', 'proxy', num, mac)
        pane.send_keys(cmd)
        print("CMD : " + cmd)
        print("Server VM"+ window_name + " started.")
        time.sleep(23)

        self.login_vm(pane, window_name)
        pane.send_keys('tmux set-option remain-on-exit on')

        """ Run VM specific postboot setup commands """
        for cmd in self.node_config.vm_manager_postboot_cmds:
            pane.send_keys(cmd)
            time.sleep(2)


        if len(self.config.vm_manager_vmspecific_postboot_cmds) > 0:
            spec_postboot_cmds = self.config.vm_manager_vmspecific_postboot_cmds[num]
        else:
            spec_postboot_cmds = []
        
        for cmd in spec_postboot_cmds:
            pane.send_keys(cmd)
            time.sleep(2)

    def run_benchmark_rpc(self, pane, stack, 
            comp_dir, comp_cmd, lib_so, exec_file, out, args):
        pane.send_keys('cd ' + comp_dir)
        pane.send_keys(comp_cmd)
        time.sleep(3)
        cmd = 'sudo '
        if stack == 'tas':
            cmd += 'LD_PRELOAD=' + lib_so + ' '
        cmd += exec_file + ' ' +  args + ' | tee ' + out
        print(cmd) 
        pane.send_keys(cmd)

        self.log_paths.append(out)

    def run_guest_proxy(self, num):
        pane = self.wmanager.add_new_pane(self.config.proxy_guest_pane,
                self.config.is_remote)

        ssh_com = self.get_ssh_command(num)

        pane.send_keys(ssh_com)
        time.sleep(2)
        pane.send_keys("tas")

        """ Run TAS proxy """
        if self.hstack == 'tas':
            time.sleep(3)
            Host.compile_and_run(
                    pane=pane,
                    comp_dir=self.node_config.guest_proxy_comp_dir,
                    comp_cmd=self.node_config.guest_proxy_comp_cmd,
                    exec_file=self.node_config.guest_proxy_exec_file,
                    out_file=self.node_config.guest_proxy_out_file,
                    args=' ',
                    bg=False)
            time.sleep(3)
            
    
    def run_guest_benchmark(self, exp, num):
        pane = self.wmanager.add_new_pane(self.config.benchmark_pane,
                self.config.is_remote)

        ssh_com = self.get_ssh_command(num)

        pane.send_keys(ssh_com)
        time.sleep(2)
        pane.send_keys("tas")
        
        """ Run Benchmark """
        if self.node_config.is_server:
            benchmark_args = self.gen_config.benchmark_server_args
        else:
            benchmark_args = self.gen_config.benchmark_client_args[num]

        self.run_benchmark_rpc(
                pane = pane,
                stack = self.hstack,
                comp_dir = self.node_config.benchmark_comp_dir,
                comp_cmd = self.node_config.benchmark_comp_cmd,
                lib_so = self.node_config.tas_bench_lib_so,
                exec_file = self.node_config.benchmark_exec_file,
                out = self.node_config.benchmark_out + '_' + exp + '_' + str(num),
                args=benchmark_args)

    def get_ssh_command(self, num):
        ip = self.get_vm_ip(num)
        if self.hstack == "linux":
            ssh_com = "ssh tas@{}".format(ip)
        else:
            ssh_com = "ssh -p 222{} tas@localhost".format(num)
        
        return ssh_com

    def get_scp_command(self, num, src_path, save_path):
        ip = self.get_vm_ip(num)
        if self.hstack == "linux":
            ssh_com = "scp tas@{}:{} {}".format(ip, src_path, save_path)
        else:
            ssh_com = "scp -P 222{} tas@localhost:{} {}".format(num, src_path, save_path)
        
        return ssh_com

    def get_vm_ip(self, i):
        if self.node_config.is_server:
            return "192.168.10.{}".format(i + 20)
        else:
            return "192.168.10.{}".format(i + 30)


class Server(Host):

    def __init__(self, wmanager, config, log_paths):
        Host.__init__(self, wmanager, config, config.server, config.snode, 
            config.stype, config.sstack, config.snum, log_paths)


class Client(Host):
    def __init__(self, wmanager, config, log_paths):
        Host.__init__(self, wmanager, config, config.client, config.cnode, 
                config.ctype, config.cstack, config.cnum, log_paths)
        self.message_size = config.msize
        self.client_num = config.cnum

    def run_benchmark(self, num, exp):
        Host.run_benchmark(self, 
                num=num, 
                exp=exp)


class WindowManager:
    def __init__(self, config):
        server = libtmux.Server()
        self.session = server.get_by_id('$0')
        self.config = config

        self.window_names = []
        self.window_names.append(config.client.setup_pane)
        self.window_names.append(config.client.tas_pane)
        self.window_names.append(config.client.proxy_pane)
        self.window_names.append(config.client.benchmark_pane)
        self.window_names.append(config.client.node_pane)
        self.window_names.append(config.client.proxy_guest_pane)
        self.window_names.append(config.client.cleanup_pane)
        self.window_names.append(config.client.save_logs_pane)

        self.window_names.append(config.server.setup_pane)
        self.window_names.append(config.server.tas_pane)
        self.window_names.append(config.server.proxy_pane)
        self.window_names.append(config.server.benchmark_pane)
        self.window_names.append(config.server.node_pane)
        self.window_names.append(config.server.proxy_guest_pane)
        self.window_names.append(config.server.cleanup_pane)
        self.window_names.append(config.server.save_logs_pane)
    
    def close_pane(self, name):
        wname  = self.config.pane_prefix + name
        while self.session.find_where({"window_name" : wname}) != None:
            self.session.find_where({"window_name" : wname}).kill_window()

    def add_new_pane(self, name, is_remote):
        wname = self.config.pane_prefix + name
        window = self.session.new_window(attach=False, window_name=wname)
        pane = window.attached_pane 
        if is_remote:
            pane.send_keys(self.config.remote_connect_cmd)
            time.sleep(2)
        return pane

    def close_panes(self):
        for wname in self.window_names:
            self.close_pane(wname)


class Experiment:

    def __init__(self,  config, name):
        self.log_paths_client = []
        self.log_paths_server = []
        self.wmanager = WindowManager(config)
        self.name = name
        self.server_host = Server(self.wmanager, config, self.log_paths_server)
        self.client_host = Client(self.wmanager, config, self.log_paths_client)
        self.config = config
        self.exp_path = ''

    def run(self):
        self.server_host.run(self.get_name())
        self.client_host.run(self.get_name())
        time.sleep(1)

    def reset(self):
        self.cleanup()
        self.wmanager.close_panes()

    def cleanup(self):
        self.server_host.run_cleanup_cmds()
        self.client_host.run_cleanup_cmds()

    def save_logs(self):
        split_path = self.exp_path.split("/")
        n = len(split_path)
        
        out_dir = os.getcwd() + "/" + "/".join(split_path[:n - 1]) + "/out"
        if not os.path.exists(out_dir):
            os.makedirs(out_dir)
       
        if self.client_host.htype == "virt":
            self.save_logs_virt(out_dir)
        else:
            self.save_logs_bare(out_dir)

    def save_logs_virt(self, out_dir):
        pane = self.wmanager.add_new_pane(self.client_host.config.save_logs_pane,
                self.client_host.config.is_remote)
        for i, path in enumerate(self.log_paths_client):
            scp_com = self.client_host.get_scp_command(i, 
                    path, out_dir + "/.")
            pane.send_keys(scp_com)
            time.sleep(2)
            pane.send_keys(suppress_history=False, cmd='tas')
            time.sleep(1)
            
    def save_logs_bare(self, out_dir):
        for path in self.log_paths_client:
            os.rename(path, out_dir + os.path.basename(path))

    def get_name(self):
        e = self.name + '-' + self.server_host.htype + '-' + \
                self.server_host.hstack + '.' + \
                self.client_host.htype + '-' + self.client_host.hstack + \
                f'{self.client_host.client_num}.' + \
                f'{self.client_host.message_size}'
        return e

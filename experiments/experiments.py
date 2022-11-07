import libtmux
import time

class Host(object):

    def __init__(self, wmanager, config, nconfig, htype, hstack, hnum):
        self.htype = htype
        self.hstack = hstack
        self.wmanager = wmanager
        self.config = config
        self.node_config = nconfig
        self.node_num = hnum

    def run_tas(self):
        pane = self.wmanager.add_new_pane(self.config.tas_pane, 
                self.config.is_remote)
        Host.compile_and_run(
                pane=pane,
                comp_dir=self.config.tas_comp_dir,
                comp_cmd=self.config.tas_comp_cmd,
                exec_file=self.config.tas_exec_file,
                out_file=self.config.tas_out_file,
                args=self.config.tas_args)
    
    def run_setup_cmds(self):
        pane = self.wmanager.add_new_pane(self.config.setup_pane,
                self.config.is_remote)
        for cmd in self.config.setup_cmds:
            pane.send_keys(cmd)
            time.sleep(1)
        self.config_pane = pane

    def run_tas_proxy(self):
        pane = self.wmanager.add_new_pane(self.config.proxy_pane,
                self.config.is_remote)
        pane.send_keys('sudo rm ' + self.config.proxy_ivshm_socket_path)
        Host.compile_and_run(
                pane=pane,
                comp_dir=self.config.proxy_comp_dir,
                comp_cmd=self.config.proxy_host_comp_cmd,
                exec_file=self.config.proxy_host_exec_file,
                out_file=self.config.proxy_host_out_file,
                args='')

    def run_vms(self, num, exp):
        window_name = self.config.node_pane
        pane = self.wmanager.add_new_pane(window_name, 
                self.config.is_remote)
        self.run_vm(
                pane=pane, 
                window_name=window_name + str(num), 
                exp=exp)

    def run_benchmark(self, num, exp):
        print("benchmark", end = " ")
        if self.config.is_server:
            print("server :", end = " ")
        else :
            print("client :", end = " ")
        pane = self.wmanager.add_new_pane(self.config.benchmark_pane, 
                self.config.is_remote)

        if self.node_config.is_server:
            benchmark_args = self.config.benchmark_server_args
        else:
            benchmark_args = self.config.benchmark_client_args
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
            bg=False):
        pane.send_keys('cd ' + comp_dir)
        pane.send_keys('git pull')
        time.sleep(1)
        pane.send_keys(comp_cmd)
        cmd = 'sudo ' + exec_file + ' ' + args
        if bg : 
            cmd += ' &  '
        cmd += ' # | tee ' + out_file
        print("Starting Server TAS: " + cmd)
        pane.send_keys(cmd)
        print("Server TAS started.")

    def run(self, exp):
        self.run_setup_cmds()
        if self.hstack == 'tas':
            self.run_tas()
            time.sleep(3)
        if self.hstack == 'tas' and self.htype == 'virt':
            self.run_tas_proxy()
            time.sleep(3)
        for i in range(self.node_num):
            if self.htype == 'virt':
                self.run_vms(i, exp=exp)
            else :
                self.run_benchmark(num=i, exp=exp)

    def login_vm(self, pane, window_name):
        print("logging in. ("+ window_name + ")")
        pane.enter()
        pane.send_keys(suppress_history=False, cmd='client')
        time.sleep(2)
        pane.send_keys(suppress_history=False, cmd='client')
        pane.enter()
        time.sleep(2)
        pane.send_keys('tmux')
        time.sleep(5)

    def run_vm(self, pane, window_name, exp, num = 0):
        """ Run preboot commands """
        for cmd in self.config.vm_manager_preboot_cmds:
            pane.send_keys(cmd(num))
            time.sleep(2)

        """ Start VM using VM-manager.sh """
        pane.send_keys('cd ' + self.config.vm_manager_dir)

        cmd = 'sudo '
        if self.hstack == 'linux':
            cmd += self.config.vm_manager('base', 'tap', num)
        else:
            cmd += self.config.vm_manager('base', 'proxy', num)
        pane.send_keys(cmd)
        print("CMD : " + cmd)
        print("Server VM"+ window_name + " started.")
        time.sleep(15)
        import pdb
        pdb.set_trace()
        self.login_vm(pane, window_name)
        pane.send_keys('tmux set-option remain-on-exit on')
        """ Run setup commands """
        for cmd in self.node_config.setup_cmds:
            pane.send_keys(cmd(num))
            time.sleep(2)

        """ Run TAS proxy """
        if self.hstack == 'tas':
            time.sleep(3)
            Host.compile_and_run(
                    pane=pane,
                    comp_dir=self.node_config.proxy_comp_dir,
                    comp_cmd=self.node_config.proxy_guest_comp_cmd,
                    exec_file=self.node_config.proxy_guest_exec_file,
                    out_file=self.node_config.proxy_guest_out_file,
                    args=' ',
                    bg=True)
            time.sleep(3)
            print("  * VM TAS proxy : " + cmd)
            time.sleep(10)
            
            pane.send_keys('tmux new-window')

        # """ Run Benchmark """
        # if self.node_config.is_server:
        #     benchmark_args = self.config.benchmark_server_args
        # else:
        #     benchmark_args = self.config.benchmark_client_args

        # self.run_benchmark_rpc(
        #         pane = pane,
        #         stack = self.hstack,
        #         comp_dir = self.node_config.benchmark_comp_dir,
        #         comp_cmd = ' ',
        #         lib_so = self.node_config.tas_lib_so,
        #         exec_file = self.node_config.benchmark_exec_file,
        #         out = self.node_config.benchmark_out + '_' + exp + '_' + str(num),
        #         args=benchmark_args)

    def run_benchmark_rpc(self, pane, stack, 
            comp_dir, comp_cmd, lib_so, exec_file, out, args):
        pane.send_keys('cd ' + comp_dir)
        pane.send_keys(comp_cmd)
        time.sleep(5)
        cmd = 'sudo '
        if stack == 'tas':
            cmd += 'LD_PRELOAD=' + lib_so + ' '
        cmd += exec_file + ' ' +  args + ' | tee ' + out
        print(cmd) 
        pane.send_keys(cmd)


class Server(Host):

    def __init__(self, wmanager, config):
        Host.__init__(self, wmanager, config.server, config.snode, 
            config.stype, config.sstack, config.snum)


class Client(Host):
    def __init__(self, wmanager, config):
        Host.__init__(self, wmanager, config.client, config.cnode, 
                config.ctype, config.cstack, config.cnum)
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

        self.window_names.append(config.server.setup_pane)
        self.window_names.append(config.server.tas_pane)
        self.window_names.append(config.server.proxy_pane)
        self.window_names.append(config.server.benchmark_pane)
        self.window_names.append(config.server.node_pane)
    
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
            time.sleep(3)
        return pane

    def close_panes(self):
        for wname in self.window_names:
            self.close_pane(wname)


class Experiment:

    def __init__(self,  config, name):
        self.wmanager = WindowManager(config)
        self.name = name
        self.server_host = Server(self.wmanager, config)
        self.client_host = Client(self.wmanager, config)

    def run(self):
        # self.server_host.run(self.get_name())
        self.client_host.run(self.get_name())

    def reset(self):
        self.wmanager.close_panes()

    def get_name(self):
        e = self.name + '-' + self.server_host.htype + '-' + \
                self.server_host.hstack + '.' + \
                self.client_host.htype + '-' + self.client_host.hstack + \
                f'{self.client_host.client_num}.' + \
                f'{self.client_host.message_size}'
        return e

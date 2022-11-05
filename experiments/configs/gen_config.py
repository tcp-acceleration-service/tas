class HostConfig:
    def __init__(self, name, is_server, is_remote):
        # general configurations
        self.is_server = is_server
        self.is_remote = is_remote
        self.project_dir = '/local/mstolet/projects/tas/'
        self.output_dir = self.project_dir + 'experiments/out/'

        # pre-start commands
        self.setup_cmds = []
        self.cleanup_cmds = []
        self.setup_pane = name + '_setup'

        # tas configurations
        self.tas_pane = name + '_tas'
        self.tas_comp_dir = self.project_dir
        self.tas_comp_cmd = 'make'
        self.tas_exec_file = self.tas_comp_dir + 'tas/tas'
        self.tas_server_out_file = self.output_dir + 'tas_s'
        self.tas_client_out_file = self.output_dir + 'tas_c'
        self.tas_out_file = ''
        self.tas_lib_so = self.tas_comp_dir + 'tas/lib/libtas_interpose.so'
        self.tas_args = ' --fp-cores-max=1' + \
            ' --cc=const-rate --cc-const-rate=0 --fp-no-ints' + \
            ' --fp-no-autoscale --dpdk-extra="-w3b:00.0"'
        if is_server:
            self.tas_args = ' --ip-addr=192.168.10.13/24' + self.tas_args
            self.tas_out_file = self.tas_server_out_file
        else:
            self.tas_args = ' --ip-addr=192.168.10.14/24' + self.tas_args
            self.tas_out_file = self.tas_client_out_file

        # tas proxy configurations
        self.proxy_ivshm_socket_path = '/run/tasproxy'
        self.proxy_pane = name + '_proxy'
        self.proxy_comp_dir = self.project_dir
        self.proxy_host_comp_cmd = 'make'
        self.proxy_host_exec_file = self.proxy_comp_dir + 'proxy/host/host'
        self.proxy_host_out_file = self.output_dir + 'proxy_h'
        self.proxy_guest_comp_cmd = 'make'
        self.proxy_guest_exec_file = self.proxy_comp_dir + 'proxy/guest'
        self.proxy_guest_out_file = self.output_dir + 'proxy_g'

        # vm manager configurations
        self.node_pane = name + '_node'
        self.vm_manager_preboot_cmds = []
        self.vm_manager_postboot_cmds = []
        self.vm_manager_dir = self.project_dir + 'virt-manager/'
        self.vm_manager = lambda machine, stack, id : self.vm_manager_dir + \
                'virtual-manager.sh' + ' ' + str(machine) + \
                ' ' + str(stack) + ' ' + str(id)

        # benchmark configurations
        self.benchmark_pane = 'benchmark_' + name
        self.benchmark_comp_dir = self.project_dir + 'build/'
        self.benchmark_comp_cmd = 'make echoserver_linux & make client_linux'
        self.benchmark_server_exec_file = self.benchmark_comp_dir + \
            'benchmarks/micro_rpc/echoserver_linux'
        self.benchmark_server_out = self.output_dir + 'rpc_s'
        self.benchmark_client_exec_file = self.benchmark_comp_dir + \
            'benchmarks/micro_rpc/client_linux'
        self.benchmark_client_out = self.output_dir + 'rpc_c'

        self.benchmark_exec_file = ''
        self.benchmark_out = ''
        if (is_server):
            self.benchmark_exec_file = self.benchmark_server_exec_file
            self.benchmark_out = self.benchmark_server_out
        else:
            self.benchmark_exec_file = self.benchmark_client_exec_file
            self.benchmark_out = self.benchmark_client_out

class HostConfig:
    def __init__(self, name, is_server, is_remote):
        # general configurations
        self.is_server = is_server
        self.is_remote = is_remote
        self.project_dir = '/local/mstolet/projects/tas/'
        self.vm_project_dir = '/home/tas/projects/tas/'
        self.output_dir = self.project_dir + 'experiments/out/'
        self.vm_output_dir = self.vm_project_dir + "experiments/out/"

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
            ' --fp-no-autoscale --dpdk-extra="-w3b:00.0" --fp-no-hugepages'
        if is_server:
            self.tas_args = ' --ip-addr=192.168.10.13/24' + self.tas_args
            self.tas_out_file = self.tas_server_out_file
        else:
            self.tas_args = ' --ip-addr=192.168.10.14/24' + self.tas_args
            self.tas_out_file = self.tas_client_out_file

        # general proxy configurations
        self.proxy_ivshm_socket_path = '/run/tasproxy'
        self.proxy_pane = name + '_proxy'

        # host proxy configurations
        self.host_proxy_comp_dir = self.project_dir
        self.host_proxy_comp_cmd = 'make'
        self.host_proxy_exec_file = self.host_proxy_comp_dir + 'proxy/host/host'
        self.host_proxy_out_file = self.output_dir + 'proxy_h'
        
        # guest proxy configurations
        self.guest_proxy_comp_dir = self.vm_project_dir
        self.guest_proxy_comp_cmd = 'make'
        self.guest_proxy_exec_file = self.guest_proxy_comp_dir + 'proxy/guest/guest'
        self.guest_proxy_out_file = self.vm_output_dir + 'proxy_g'

        # vm manager configurations
        self.node_pane = name + '_node'
        self.vm_manager_preboot_cmds = []
        self.vm_manager_postboot_cmds = [
            "sudo su -",
            "sudo echo 1 > /sys/module/vfio/parameters/enable_unsafe_noiommu_mode",
            "sudo echo 1af4 1110 > /sys/bus/pci/drivers/vfio-pci/new_id",
            "exit"
        ]
        self.vm_manager_dir = self.project_dir + 'images/'
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

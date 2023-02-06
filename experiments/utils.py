import time

def get_ssh_command(machine_config, vm_config):
    stack = machine_config.stack
    if stack == "virt-tas":
        ssh_com = "ssh -p 222{} tas@localhost".format(vm_config.id)
    else:
        ssh_com = "ssh tas@{}".format(vm_config.vm_ip)
    
    return ssh_com

def get_scp_command(machine_config, vm_config, src_path, save_path):
    ip = vm_config.vm_ip
    idx = vm_config.id
    if machine_config.stack == "virt-tas":
        ssh_com = "scp -P 222{} tas@localhost:{} {}".format(idx, src_path, save_path)
    else:
        ssh_com = "scp tas@{}:{} {}".format(ip, src_path, save_path)
    
    return ssh_com

def compile_and_run(pane, comp_dir, comp_cmd, 
        exec_file, args, out,
        bg=False, gdb=False, 
        break_file=None, line_break=None):

    pane.send_keys('cd ' + comp_dir)
    pane.send_keys('git pull')
    time.sleep(1)
    pane.send_keys(comp_cmd)

    if gdb:
        cmd = 'sudo gdb --args ' + exec_file + ' ' + args
    else:
        cmd = 'sudo ' + exec_file + ' ' + args
        # cmd += ' | tee ' + out

    if bg : 
        cmd += ' &  '

    pane.send_keys(cmd)

    if break_file and line_break:
        pane.send_keys("break {}:{}".format(break_file, line_break))

    if gdb:
        pane.send_keys("run")

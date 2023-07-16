import time

class Node:
  
  def __init__(self, defaults, machine_config, 
      wmanager, setup_pane_name, cleanup_pane_name):
    self.defaults = defaults
    self.machine_config = machine_config
    self.wmanager = wmanager
    self.setup_pane_name = setup_pane_name
    self.cleanup_pane_name = cleanup_pane_name

  def setup(self):
    self.setup_pane = self.wmanager.add_new_pane(self.setup_pane_name, 
        self.machine_config.is_remote)

  def cleanup(self):
    self.cleanup_pane = self.wmanager.add_new_pane(self.cleanup_pane_name, 
        self.machine_config.is_remote)

  def tap_up(self, interface, script_dir, multi_queue):
    cmd = "cd {}".format(script_dir)
    self.setup_pane.send_keys(cmd)
    time.sleep(1)
    cmd = "sudo bash tap-up.sh {} {}".format(interface, multi_queue)
    self.setup_pane.send_keys(cmd)
    time.sleep(2)

  def tap_down(self, interface, script_dir):
    cmd = "cd {}".format(script_dir)
    self.cleanup_pane.send_keys(cmd)
    time.sleep(1)
    cmd = "sudo bash tap-down.sh {}".format(interface)
    self.cleanup_pane.send_keys(cmd)
    time.sleep(2)

  # Args are the ip for the interface TAS normally used
  # and its ip
  def bridge_up(self, ip, interface, script_dir):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash br-up.sh {} {}/24".format(interface, ip)
      self.setup_pane.send_keys(cmd)
      time.sleep(2)
  
  # Args are the ip for the interface TAS normally used
  # and its ip
  def bridge_down(self, ip, interface, script_dir):
      cmd = "cd {}".format(script_dir)
      self.cleanup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash br-down.sh {} {}/24".format(interface, ip)
      self.cleanup_pane.send_keys(cmd)
      time.sleep(2)

  def ovs_make_uninstall(self, ovs_mod_dir, ovs_o_dir):
      cmd = "cd {}".format(ovs_o_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo make uninstall"
      self.setup_pane.send_keys(cmd)
      time.sleep(1)

      cmd = "cd {}".format(ovs_mod_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo make uninstall"
      self.setup_pane.send_keys(cmd)
      time.sleep(1)

  def ovs_make_install(self, ovs_dir):
      cmd = "cd {}".format(ovs_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo make install"
      self.setup_pane.send_keys(cmd)
      time.sleep(3)

  def start_ovs(self, script_dir):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovs-start.sh"
      self.setup_pane.send_keys(cmd)
      time.sleep(4)

  def stop_ovs(self, script_dir):
      cmd = "cd {}".format(script_dir)
      self.cleanup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovs-stop.sh"
      self.cleanup_pane.send_keys(cmd)
      time.sleep(2)

  def start_ovsdpdk(self, script_dir):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovsdpdk-start.sh"
      self.setup_pane.send_keys(cmd)
      time.sleep(4)

  def stop_ovsdpdk(self, script_dir):
      cmd = "cd {}".format(script_dir)
      self.cleanup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovsdpdk-stop.sh"
      self.cleanup_pane.send_keys(cmd)
      time.sleep(2)

  def ovsbr_add(self, br_name, ip, interface, script_dir):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovsbr-add.sh {} {} {}".format(br_name, ip, interface)
      self.setup_pane.send_keys(cmd)
      time.sleep(4)

  def ovsbr_add_vtuoso(self, br_name, script_dir):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovs-add-vtuosobr.sh {}".format(br_name)
      self.setup_pane.send_keys(cmd)
      time.sleep(4)

  def ovsbr_del(self, br_name):
      cmd = "sudo ovs-vsctl del-br {}".format(br_name)
      self.cleanup_pane.send_keys(cmd)
      time.sleep(2)
  
  def ovsvhost_add(self, br_name, vhost_name, 
                   gre_name, remote_ip, gre_key, 
                   script_dir):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovsvhost-add.sh {} {} {} {} {}".format(
          br_name, vhost_name, gre_name, remote_ip, gre_key)
      self.setup_pane.send_keys(cmd)
      time.sleep(4)

  def ovsport_add_vtuoso(self, br_name, port_name, port_type, vmid, script_dir,
                         out_remote_ip=None, out_local_ip=None,
                         in_remote_ip=None, in_local_ip=None, key=None):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      if port_type == "virtuosotx":
        cmd = "sudo bash ovs-add-vtuosoport.sh {} {} {} {} {} {} {} {} {}".format(
            br_name, port_name, port_type, vmid,
            out_remote_ip, out_local_ip, in_remote_ip, in_local_ip, key) 
      else:
        cmd = "sudo bash ovs-add-vtuosoport.sh {} {} {} {}".format(
            br_name, port_name, port_type, vmid)
      self.setup_pane.send_keys(cmd)
      time.sleep(2)

  def ovstunnel_add(self, br_name, tun_name, remote_ip, script_dir, key=None):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      if key is None:
        cmd = "sudo bash ovs-add-tunnel.sh {} {} {}".format(
            br_name, tun_name, remote_ip)
      else:
         cmd = "sudo bash ovs-add-tunnel.sh {} {} {} {}".format(
            br_name, tun_name, remote_ip, key)
      self.setup_pane.send_keys(cmd)
      time.sleep(2)    

  def ovstap_add(self, br_name, tap_name, multi_queue, script_dir):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovstap-add.sh {} {} {}".format(
          br_name, tap_name, multi_queue)
      self.setup_pane.send_keys(cmd)
      time.sleep(2)

  def ovstap_del(self, script_dir):
      cmd = "cd {}".format(script_dir)
      self.cleanup_pane.send_keys(cmd)
      time.sleep(1)

  def ovsflow_add(self, br_name, in_port, out_port, script_dir):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovsflow-add.sh {} {} {}".format(br_name, in_port, out_port)
      self.setup_pane.send_keys(cmd)
      time.sleep(2)

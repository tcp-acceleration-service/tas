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


  def ovs_make_install(self, ovs_dir):
      cmd = "cd {}".format(ovs_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo make install"
      self.setup_pane.send_keys(cmd)
      time.sleep(4)

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

  def ovsbr_add(self, br_name, ip, interface, script_dir):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovsbr-add.sh {} {} {}".format(br_name, ip, interface)
      self.setup_pane.send_keys(cmd)
      time.sleep(4)

  def ovsbr_del(self, br_name, script_dir):
      cmd = "cd {}".format(script_dir)
      self.cleanup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovsbr-del.sh {}".format(br_name)
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

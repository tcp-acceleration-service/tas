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
    cmd = "sudo bash tap_up.sh {} {}".format(interface, multi_queue)
    self.setup_pane.send_keys(cmd)
    time.sleep(2)

  def tap_down(self, interface, script_dir):
    cmd = "cd {}".format(script_dir)
    self.cleanup_pane.send_keys(cmd)
    time.sleep(1)
    cmd = "sudo bash tap_down.sh {}".format(interface)
    self.cleanup_pane.send_keys(cmd)
    time.sleep(2)

  # Args are the ip for the interface TAS normally used
  # and its ip
  def bridge_up(self, ip, interface, script_dir):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash br_up.sh {} {}/24".format(interface, ip)
      self.setup_pane.send_keys(cmd)
      time.sleep(2)
  
  # Args are the ip for the interface TAS normally used
  # and its ip
  def bridge_down(self, ip, interface, script_dir):
      cmd = "cd {}".format(script_dir)
      self.cleanup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash br_down.sh {} {}/24".format(interface, ip)
      self.cleanup_pane.send_keys(cmd)
      time.sleep(2)
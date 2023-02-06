import time
from nodes.node import Node

class BareLinux(Node):
  
  def __init__(self, defaults, machine_config,
      wmanager, setup_pane_name, cleanup_pane_name):

    Node.__init__(self, defaults, machine_config, wmanager, 
        setup_pane_name, cleanup_pane_name)
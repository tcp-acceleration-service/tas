import time
from nodes.node import Node

class VTasBare(Node):
  
  def __init__(self, defaults, machine_config, tas_config,
      wmanager, setup_pane_name, cleanup_pane_name):

    Node.__init__(self, defaults, machine_config, wmanager, 
        setup_pane_name, cleanup_pane_name)
        
    self.tas_config = tas_config
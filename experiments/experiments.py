import time

from window_manager import WindowManager
from nodes.virt_tas.vtas_server import VirtTasServer
from nodes.virt_tas.vtas_client import VirtTasClient
from nodes.bare_tas.btas_server import BareTasServer
from nodes.bare_tas.btas_client import BareTasClient
from nodes.virt_linux.vlinux_server import VirtLinuxServer
from nodes.virt_linux.vlinux_client import VirtLinuxClient
from nodes.bare_linux.blinux_server import BareLinuxServer
from nodes.bare_linux.blinux_client import BareLinuxClient
from nodes.tap_tas.ttas_server import TapTasServer
from nodes.tap_tas.ttas_client import TapTasClient
from nodes.vtas_bare.vtasbare_server import VTasBareServer
from nodes.vtas_bare.vtasbare_client import VTasBareClient

class Experiment:

    def __init__(self,  config, name):
        self.wmanager = WindowManager(config)
        self.name = name
        self.config = config
        self.exp_path = ''
        self.snode = self.init_server_node(self.config.sstack)
        self.cnode = self.init_client_node(self.config.cstack)

    def init_server_node(self, stack):
        if stack == "virt-tas":
            node = VirtTasServer(self.config, self.wmanager)
        elif stack == "bare-tas":
            node = BareTasServer(self.config, self.wmanager)
        elif stack == "virt-linux":
            node = VirtLinuxServer(self.config, self.wmanager)
        elif stack == "bare-linux":
            node = BareLinuxServer(self.config, self.wmanager)
        elif stack == "tap-tas":
            node = TapTasServer(self.config, self.wmanager)
        elif stack == "bare-vtas":
            node = VTasBareServer(self.config, self.wmanager)

        return node

    def init_client_node(self, stack):
        if stack == "virt-tas":
            node = VirtTasClient(self.config, self.wmanager)
        elif stack == "bare-tas":
            node = BareTasClient(self.config, self.wmanager)
        elif stack == "virt-linux":
            node = VirtLinuxClient(self.config, self.wmanager)
        elif stack == "bare-linux":
            node = BareLinuxClient(self.config, self.wmanager)
        elif stack == "tap-tas":
            node = TapTasClient(self.config, self.wmanager)
        elif stack == "bare-vtas":
            node = VTasBareClient(self.config, self.wmanager)

        return node

    def run(self):
        self.snode.run()
        self.cnode.run()

    def reset(self):
        self.cleanup()
        self.wmanager.close_panes()

    def cleanup(self):
        self.cnode.cleanup()
        self.snode.cleanup()
        time.sleep(2)

    def save_logs(self):
        self.cnode.save_logs(self.exp_path)

    def get_name(self):
        e = self.name + '-' + self.config.sstack + '.' + self.config.cstack
        return e
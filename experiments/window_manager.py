import time
import libtmux

class WindowManager:
    def __init__(self, config):
        server = libtmux.Server()
        self.session = server.attached_sessions[0]
        self.config = config
        self.window_names = self.init_window_names()

    def init_window_names(self):
        window_names = []

        window_names.append(self.config.defaults.c_tas_pane)
        window_names.append(self.config.defaults.c_vm_pane)
        window_names.append(self.config.defaults.c_proxyg_pane)
        window_names.append(self.config.defaults.c_proxyh_pane)
        window_names.append(self.config.defaults.c_client_pane)
        window_names.append(self.config.defaults.c_savelogs_pane)
        window_names.append(self.config.defaults.c_setup_pane)
        window_names.append(self.config.defaults.c_cleanup_pane)
        
        window_names.append(self.config.defaults.s_tas_pane)
        window_names.append(self.config.defaults.s_vm_pane)
        window_names.append(self.config.defaults.s_proxyg_pane)
        window_names.append(self.config.defaults.s_proxyh_pane)
        window_names.append(self.config.defaults.s_server_pane)
        window_names.append(self.config.defaults.s_savelogs_pane)
        window_names.append(self.config.defaults.s_setup_pane)
        window_names.append(self.config.defaults.s_cleanup_pane)

        return window_names

    def close_pane(self, name):
        try:
            windows = self.session.windows.filter(window_name=name)
            for window in windows:
                window.kill_window()
        except libtmux._internal.query_list.ObjectDoesNotExist:
            return

    def add_new_pane(self, name, is_remote):
        window = self.session.new_window(attach=False, window_name=name)
        pane = window.attached_pane 
        if is_remote:
            pane.send_keys(self.config.defaults.remote_connect_cmd)
            time.sleep(2)

        return pane

    def close_panes(self):
        for wname in self.window_names:
            self.close_pane(wname)
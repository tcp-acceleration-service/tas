import experiments as exp

from exps.perf_iso_tpconn.configs.bare_tas import Config as TasBareConf
from exps.perf_iso_tpconn.configs.bare_vtas import Config as VTasBareConf
from exps.perf_iso_tpconn.configs.virt_tas import Config as TasVirtConf
from exps.perf_iso_tpconn.configs.ovs_linux import Config as OVSLinuxConf
from exps.perf_iso_tpconn.configs.ovs_tas import Config as OVSTasConf

experiments = []

n_conns = [8]
n_runs = 1
batch_size = 4

for n_r in range(n_runs):
  for n_c in n_conns:
    exp_name = "perf-iso-tpconn-run{}-conns{}_stas_bsize{}_".format(n_r, n_c, batch_size)
    tas_bare_exp = exp.Experiment(TasBareConf(exp_name + "bare-tas", n_c), name=exp_name)

    experiments.append(tas_bare_exp)
  

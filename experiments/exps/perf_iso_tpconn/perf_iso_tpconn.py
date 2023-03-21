import experiments as exp

from exps.perf_iso_tpconn.configs.bare_tas import Config as TasBareConf
from exps.perf_iso_tpconn.configs.bare_vtas import Config as VTasBareConf
from exps.perf_iso_tpconn.configs.virt_tas import Config as TasVirtConf
from exps.perf_iso_tpconn.configs.ovs_linux import Config as OVSLinuxConf

experiments = []

# n_conns = [128, 256, 512, 1024, 2048, 4096, 8192]
n_conns = [128]

for n in n_conns:
  exp_name = "perf-iso-tpconn_conns{}_".format(n)
  tas_bare_exp = exp.Experiment(TasBareConf(exp_name + "bare-tas", n), name=exp_name)
  vtas_bare_exp = exp.Experiment(VTasBareConf(exp_name + "bare-vtas", n), name=exp_name)
  tas_virt_exp = exp.Experiment(TasVirtConf(exp_name + "virt-tas", n), name=exp_name)
  ovs_linux_exp = exp.Experiment(OVSLinuxConf(exp_name + "ovs-linux", n), name=exp_name)

  # experiments.append(tas_bare_exp)
  # experiments.append(vtas_bare_exp)
  # experiments.append(tas_virt_exp)
  experiments.append(ovs_linux_exp)
  

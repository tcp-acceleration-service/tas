import experiments as exp

from exps.perf_iso_tpmsize.configs.bare_tas import Config as TasBareConf
from exps.perf_iso_tpmsize.configs.bare_vtas import Config as VTasBareConf
from exps.perf_iso_tpmsize.configs.virt_tas import Config as TasVirtConf
from exps.perf_iso_tpmsize.configs.ovs_linux import Config as OVSLinuxConf
from exps.perf_iso_tpmsize.configs.ovs_tas import Config as OVSTasConf

experiments = []

# msize = [64, 128, 256, 512, 1024, 2048]
msize = [2048]

for n in msize:
  exp_name = "perf-iso-tpconn-msize{}_".format(n)
  tas_bare_exp = exp.Experiment(TasBareConf(exp_name + "bare-tas", n), name=exp_name)
  vtas_bare_exp = exp.Experiment(VTasBareConf(exp_name + "bare-vtas", n), name=exp_name)
  tas_virt_exp = exp.Experiment(TasVirtConf(exp_name + "virt-tas", n), name=exp_name)
  ovs_linux_exp = exp.Experiment(OVSLinuxConf(exp_name + "ovs-linux", n), name=exp_name)
  ovs_tas_exp = exp.Experiment(OVSTasConf(exp_name + "ovs-tas", n), name=exp_name)

  experiments.append(tas_bare_exp)
  # experiments.append(vtas_bare_exp)
  # experiments.append(tas_virt_exp)
  # experiments.append(ovs_tas_exp)
  # experiments.append(ovs_linux_exp)
  
